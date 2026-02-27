#include "memory.h"
#include "value.h"
#include "vm.h"
#include "table.h"
#include "chunk.h"
#include <cstdlib>
#include <cstdio>

size_t tantrums_bytes_allocated = 0;
size_t tantrums_peak_bytes_allocated = 0;
size_t tantrums_next_gc = 1024 * 1024;

extern Obj* all_objects;
extern VM* current_vm_for_gc;

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size) {
    tantrums_bytes_allocated += new_size;
    tantrums_bytes_allocated -= old_size;
    
    if (tantrums_bytes_allocated > tantrums_peak_bytes_allocated) {
        tantrums_peak_bytes_allocated = tantrums_bytes_allocated;
    }

    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
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
    tantrums_bytes_allocated = 0;
}
