#include "memory.h"
#include "value.h"
#include "vm.h"
#include "table.h"
#include "chunk.h"
#include <cstdlib>
#include <cstdio>

size_t tantrums_bytes_allocated = 0;
size_t tantrums_next_gc = 1024 * 1024;

extern Obj* all_objects;
extern VM* current_vm_for_gc;

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) { free(ptr); return nullptr; }
    return realloc(ptr, new_size);
}

static Obj** gray_stack = nullptr;
static int gray_count = 0;
static int gray_capacity = 0;

static void mark_object_gray(Obj* object) {
    if (!object || object->is_marked) return;
#if DEBUG_LOG_GC
    printf("Marking %p\n", (void*)object);
#endif
    object->is_marked = true;

    if (gray_capacity < gray_count + 1) {
        gray_capacity = gray_capacity < 8 ? 8 : gray_capacity * 2;
        gray_stack = (Obj**)realloc(gray_stack, sizeof(Obj*) * gray_capacity);
    }
    gray_stack[gray_count++] = object;
}

static void mark_value_gray(Value value) {
    if (IS_OBJ(value)) mark_object_gray(AS_OBJ(value));
}

static void mark_array(Value* array, int count) {
    for (int i = 0; i < count; i++) {
        mark_value_gray(array[i]);
    }
}

static void mark_table(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        TableEntry* entry = &table->entries[i];
        if (entry->key) {
            mark_object_gray((Obj*)entry->key);
            mark_value_gray(entry->value);
        }
    }
}

static void blacken_object(Obj* object) {
    switch (object->type) {
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            mark_array(list->items, list->count);
            break;
        }
        case OBJ_MAP: {
            ObjMap* map = (ObjMap*)object;
            for (int i = 0; i < map->capacity; i++) {
                if (map->entries[i].occupied) {
                    mark_object_gray((Obj*)map->entries[i].key);
                    mark_value_gray(map->entries[i].value);
                }
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* func = (ObjFunction*)object;
            if (func->name) mark_object_gray((Obj*)func->name);
            if (func->chunk) mark_array(func->chunk->constants, func->chunk->const_count);
            break;
        }
        case OBJ_POINTER: {
            ObjPointer* p = (ObjPointer*)object;
            if (p->is_valid && p->target) {
                mark_value_gray(*p->target);
            }
            break;
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void trace_references() {
    while (gray_count > 0) {
        Obj* object = gray_stack[--gray_count];
        blacken_object(object);
    }
}

static void sweep() {
    Obj* previous = nullptr;
    Obj* object = all_objects;
    while (object != nullptr) {
        if (object->is_marked || object->is_manual) {
            object->is_marked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != nullptr) {
                previous->next = object;
            } else {
                all_objects = object;
            }
            
            size_t size = 0;
            switch(unreached->type) {
                case OBJ_STRING: size = sizeof(ObjString) + ((ObjString*)unreached)->capacity + 1; break;
                case OBJ_LIST: size = sizeof(ObjList) + sizeof(Value) * ((ObjList*)unreached)->capacity; break;
                case OBJ_MAP: size = sizeof(ObjMap) + sizeof(MapEntry) * ((ObjMap*)unreached)->capacity; break;
                case OBJ_FUNCTION: size = sizeof(ObjFunction); break;
                case OBJ_NATIVE: size = sizeof(ObjNative); break;
                case OBJ_POINTER: size = sizeof(ObjPointer); break;
            }
            if (tantrums_bytes_allocated >= size) {
                tantrums_bytes_allocated -= size;
            } else {
                tantrums_bytes_allocated = 0;
            }
            
            obj_free(unreached);
        }
    }
}



static void mark_roots() {
    if (!current_vm_for_gc) return;
    VM* vm = current_vm_for_gc;
    
    for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
        mark_value_gray(*slot);
    }
    
    for (int i = 0; i < vm->frame_count; i++) {
        mark_object_gray((Obj*)vm->frames[i].function);
    }
    
    mark_table(&vm->globals);
    
    for (int i = 0; i < vm->handler_count; i++) {
        ExceptionHandler* h = &vm->handlers[i];
        if (h->stack_top > vm->stack) {
            for (Value* slot = vm->stack; slot < h->stack_top; slot++) {
                mark_value_gray(*slot);
            }
        }
    }
}

void tantrums_gc_collect(void) {
    // GC Disabled. We only clean up at program exit.
}

void tantrums_free_all_objects(void) {
    Obj* obj = all_objects;
    while (obj) {
        Obj* next = obj->next;
        obj_free(obj);
        obj = next;
    }
    all_objects = nullptr;
    
    if (gray_stack) {
        free(gray_stack);
        gray_stack = nullptr;
    }
    gray_capacity = 0;
    gray_count = 0;
    tantrums_bytes_allocated = 0;
}
