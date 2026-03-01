#include "vm.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include "builtins.h"
#include "memory.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>

VM* current_vm_for_gc = nullptr;
bool global_allow_leaks = false;
bool global_autofree = true;

void vm_init(VM* vm) {
    memset(vm, 0, sizeof(VM));
    vm->stack_top = vm->stack;
    table_init(&vm->globals);
    builtins_register(vm);
}

static void vm_exit_scope(VM* vm) {
    if (vm->scope_depth > 0) {
        vm->scope_depth--;
        /* Scan all live pointers: if allocated in this scope and not escaped, free it */
        extern Obj* all_objects;
        Obj* o = all_objects;
        
        Obj* marker = nullptr;
        if (vm->scope_depth < MAX_LOCAL_SCOPES) {
            marker = vm->scope_alloc_markers[vm->scope_depth];
        }

        Obj* prev = nullptr;
        while (o && o != marker) {
            Obj* next_node = o->next;
            if (o->type == OBJ_POINTER) {
                ObjPointer* p = (ObjPointer*)o;
                if (p->auto_manage && global_autofree && p->is_valid && p->target && !p->escaped && p->scope_depth > vm->scope_depth) {
                    extern bool suppress_autofree_notes;
                    if (!suppress_autofree_notes) {

                        const char* func_name = p->alloc_func ? p->alloc_func->chars : "main";
                        const char* type_name = p->alloc_type ? p->alloc_type->chars : "dynamic";
                        int line = p->alloc_line;
                        size_t size = p->alloc_size;
                        
                        bool found = false;
                        for (int i = 0; i < vm->auto_free_count; i++) {
                            AutoFreeRecord* rec = &vm->auto_free_records[i];
                            if (rec->line == line && rec->size == size && strcmp(rec->func_name, func_name) == 0 && strcmp(rec->type_name, type_name) == 0) {
                                rec->count++;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            if (vm->auto_free_count >= vm->auto_free_capacity) {
                                vm->auto_free_capacity = vm->auto_free_capacity < 8 ? 8 : vm->auto_free_capacity * 2;
                                vm->auto_free_records = (AutoFreeRecord*)realloc(vm->auto_free_records, sizeof(AutoFreeRecord) * vm->auto_free_capacity);
                            }
                            AutoFreeRecord* new_rec = &vm->auto_free_records[vm->auto_free_count++];
                            new_rec->func_name = func_name;
                            new_rec->type_name = type_name;
                            new_rec->line = line;
                            new_rec->size = size;
                            new_rec->count = 1;
                        }
                        vm->total_auto_frees++;
                    }
                    free(p->target);
                    p->target = nullptr;
                    p->is_valid = false;
                    
                    /* Physically Unlink ObjPointer Structure from Linked List */
                    if (prev == nullptr) all_objects = next_node;
                    else prev->next = next_node;
                    
                    extern void* tantrums_realloc(void* pointer, size_t oldSize, size_t newSize);
                    tantrums_realloc(o, sizeof(ObjPointer), 0);
                    
                    o = next_node;
                    continue; /* Do not advance prev since current was removed */
                }
            }
            /* Runtime auto-free for non-escaped local lists */
            if (o->type == OBJ_LIST) {
                ObjList* lst = (ObjList*)o;
                if (lst->auto_manage && global_autofree && !lst->escaped && lst->scope_depth > vm->scope_depth) {
                    if (lst->items) {
                        free(lst->items);
                        lst->items = nullptr;
                        lst->count = 0;
                        lst->capacity = 0;
                    }
                    if (prev == nullptr) all_objects = next_node;
                    else prev->next = next_node;
                    extern void* tantrums_realloc(void* pointer, size_t oldSize, size_t newSize);
                    tantrums_realloc(o, sizeof(ObjList), 0);
                    o = next_node;
                    continue;
                }
            }
            /* Runtime auto-free for non-escaped local maps */
            if (o->type == OBJ_MAP) {
                ObjMap* mp = (ObjMap*)o;
                if (mp->auto_manage && global_autofree && !mp->escaped && mp->scope_depth > vm->scope_depth) {
                    if (mp->entries) {
                        free(mp->entries);
                        mp->entries = nullptr;
                        mp->count = 0;
                        mp->capacity = 0;
                    }
                    if (prev == nullptr) all_objects = next_node;
                    else prev->next = next_node;
                    extern void* tantrums_realloc(void* pointer, size_t oldSize, size_t newSize);
                    tantrums_realloc(o, sizeof(ObjMap), 0);
                    o = next_node;
                    continue;
                }
            }
            prev = o;
            o = next_node;
        }
    }
}

void vm_free(VM* vm) {
    extern bool global_autofree;
    if (global_autofree && vm->total_auto_frees > 0) {
        FILE* out_file = stdout;
        bool write_to_file = vm->total_auto_frees > 20;

        if (write_to_file) {
            extern const char* current_bytecode_path;
            char log_path[1024] = "autoFree.txt";
            if (current_bytecode_path) {
                const char* last_slash = strrchr(current_bytecode_path, '/');
                const char* last_bslash = strrchr(current_bytecode_path, '\\');
                const char* dir_end = last_slash > last_bslash ? last_slash : last_bslash;
                if (dir_end) {
                    size_t dir_len = dir_end - current_bytecode_path + 1;
                    if (dir_len < sizeof(log_path)) {
                        strncpy(log_path, current_bytecode_path, dir_len);
                        log_path[dir_len] = '\0';
                        strncat(log_path, "autoFree.txt", sizeof(log_path) - dir_len - 1);
                    }
                }
            }
            out_file = fopen(log_path, "w");
            if (!out_file) {
                out_file = stdout; // fallback
                printf("Failed to open autoFree.txt for writing.\n");
            }
        }

        const char* exec_name = "unknown";
        extern const char* current_bytecode_path;
        if (current_bytecode_path) {
            const char* last_slash = strrchr(current_bytecode_path, '/');
            const char* last_bslash = strrchr(current_bytecode_path, '\\');
            const char* start = last_slash > last_bslash ? last_slash : last_bslash;
            exec_name = start ? start + 1 : current_bytecode_path;
        }

        fprintf(out_file, "\nTANTRUMS AUTO-FREE REPORT\n");
        fprintf(out_file, "============================\n");
        fprintf(out_file, "Executable: %s\n", exec_name);
        fprintf(out_file, "Auto-Frees: %d allocations\n", vm->total_auto_frees);
        fprintf(out_file, "============================\n");

        size_t total_bytes = 0;
        for (int i = 0; i < vm->auto_free_count; i++) {
            AutoFreeRecord* rec = &vm->auto_free_records[i];
            total_bytes += rec->size * rec->count;
            if (rec->count > 1) {
                fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes) [x%d]\n",
                        rec->line, rec->func_name, rec->type_name, rec->size, rec->count);
            } else {
                fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes)\n",
                        rec->line, rec->func_name, rec->type_name, rec->size);
            }
        }
        
        char formatted_bytes[64];
        snprintf(formatted_bytes, sizeof(formatted_bytes), "%zu", total_bytes);
        int len = (int)strlen(formatted_bytes);
        char comma_bytes[64];
        int out_idx = 0;
        for (int i = 0; i < len; i++) {
            if (i > 0 && (len - i) % 3 == 0) {
                if (out_idx < sizeof(comma_bytes) - 1) comma_bytes[out_idx++] = ',';
            }
            if (out_idx < sizeof(comma_bytes) - 1) comma_bytes[out_idx++] = formatted_bytes[i];
        }
        comma_bytes[out_idx] = '\0';

        fprintf(out_file, "============================\n");
        fprintf(out_file, "SUMMARY\n");
        fprintf(out_file, "  Total auto-freed: %s bytes\n", comma_bytes);
        if (total_bytes >= 1024) {
            fprintf(out_file, "                  = %.2f KB\n", (double)total_bytes / 1024.0);
        }
        if (total_bytes >= 1024 * 1024) {
             fprintf(out_file, "                  = %.2f MB\n", (double)total_bytes / (1024.0 * 1024.0));
        }
        fprintf(out_file, "============================\n");
        
        if (out_file != stdout) {
            fclose(out_file);
        } else {
            fprintf(stdout, "\n");
        }
    }
    
    if (vm->auto_free_records) {
        free(vm->auto_free_records);
        vm->auto_free_records = nullptr;
    }

    if (current_vm_for_gc == vm) current_vm_for_gc = nullptr;
    table_free(&vm->globals);
    tantrums_free_all_objects();
}

void vm_push(VM* vm, Value value) {
    if (vm->stack_top - vm->stack >= MAX_STACK) {
        fprintf(stderr, "Stack overflow!\n"); exit(1);
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM* vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

Value vm_peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

bool vm_runtime_error(VM* vm, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (vm->handler_count > 0) {
        ExceptionHandler* h = &vm->handlers[--vm->handler_count];
        vm->stack_top = h->stack_top;
        vm->frame_count = h->frame_count;
        CallFrame* frame = &vm->frames[vm->frame_count - 1];
        frame->ip = h->catch_ip;
        vm_push(vm, OBJ_VAL(obj_string_new(buffer, (int)strlen(buffer))));
        return true; // Error was caught, continue execution
    }

    fprintf(stderr, "\n[Tantrums Runtime Error] %s\n", buffer);
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* fn = frame->function;
        int offset = (int)(frame->ip - fn->chunk->code - 1);
        int line = fn->chunk->lines[offset];
        fprintf(stderr, "  [line %d] in %s\n", line, fn->name ? fn->name->chars : "<script>");
    }
    return false; // Fatal error
}

static bool is_falsy(VM* vm, Value v, bool* error) {
    *error = false;
    if (IS_BOOL(v)) return !AS_BOOL(v);
    *error = true;
    return false;
}

static Value num_add(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return INT_VAL(AS_INT(a) + AS_INT(b));
    return FLOAT_VAL(value_as_number(a) + value_as_number(b));
}
static Value num_sub(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return INT_VAL(AS_INT(a) - AS_INT(b));
    return FLOAT_VAL(value_as_number(a) - value_as_number(b));
}
static Value num_mul(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) return INT_VAL(AS_INT(a) * AS_INT(b));
    return FLOAT_VAL(value_as_number(a) * value_as_number(b));
}
static bool num_div(VM* vm, Value a, Value b, Value* out) {
    if (IS_INT(a) && IS_INT(b)) {
        if (AS_INT(b) == 0) {
            vm_runtime_error(vm, "Division by zero.");
            return false;
        }
        *out = INT_VAL(AS_INT(a) / AS_INT(b));
        return true;
    }
    double d = value_as_number(b);
    if (d == 0) {
        vm_runtime_error(vm, "Division by zero.");
        return false;
    }
    *out = FLOAT_VAL(value_as_number(a) / d);
    return true;
}
static bool num_mod(VM* vm, Value a, Value b, Value* out) {
    /* Coerce floats that are whole numbers to int for modulo */
    int64_t ia, ib;
    if (IS_INT(a)) ia = AS_INT(a);
    else if (IS_FLOAT(a) && AS_FLOAT(a) == (int64_t)AS_FLOAT(a)) ia = (int64_t)AS_FLOAT(a);
    else { vm_runtime_error(vm, "Modulo operands must be integers (got float '%g').", value_as_number(a)); return false; }
    if (IS_INT(b)) ib = AS_INT(b);
    else if (IS_FLOAT(b) && AS_FLOAT(b) == (int64_t)AS_FLOAT(b)) ib = (int64_t)AS_FLOAT(b);
    else { vm_runtime_error(vm, "Modulo operands must be integers (got float '%g').", value_as_number(b)); return false; }
    if (ib == 0) { vm_runtime_error(vm, "Modulo by zero."); return false; }
    *out = INT_VAL(ia % ib);
    return true;
}

static int64_t range_length(ObjRange* r) {
    return r->length;
}

static bool call_value(VM* vm, Value callee, int argc) {
    if (IS_FUNCTION(callee)) {
        ObjFunction* fn = AS_FUNCTION(callee);
        if (argc != fn->arity) {
            if (!vm_runtime_error(vm, "'%s' expected %d args but got %d.", fn->name ? fn->name->chars : "?", fn->arity, argc))
                return false;
            return true; // if caught, proceed
        }
        if (vm->frame_count >= MAX_FRAMES) {
            if (!vm_runtime_error(vm, "Stack overflow (too many calls)."))
                return false;
            return true;
        }
        CallFrame* frame = &vm->frames[vm->frame_count++];
        frame->function = fn;
        frame->ip = fn->chunk->code;
        frame->slots = vm->stack_top - argc - 1;
        frame->saved_scope_depth = vm->scope_depth;
        return true;
    }
    if (IS_NATIVE(callee)) {
        ObjNative* native = AS_NATIVE(callee);
        Value result = native->function(vm, argc, vm->stack_top - argc);
        vm->stack_top -= argc + 1;
        vm_push(vm, result);
        return true;
    }
    if (!vm_runtime_error(vm, "Can only call functions."))
        return false;
    return true;
}

static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONST_IDX() (frame->ip += 2, (uint16_t)(frame->ip[-2] | ((uint16_t)frame->ip[-1] << 8)))
#define READ_CONSTANT() (frame->function->chunk->constants[READ_CONST_IDX()])

    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
        case OP_CONSTANT: vm_push(vm, READ_CONSTANT()); break;
        case OP_NULL:  vm_push(vm, NULL_VAL); break;
        case OP_TRUE:  vm_push(vm, BOOL_VAL(true)); break;
        case OP_FALSE: vm_push(vm, BOOL_VAL(false)); break;

        case OP_ADD: {
            Value b = vm_peek(vm, 0);
            Value a = vm_peek(vm, 1);
            if (IS_STRING(a) && IS_STRING(b)) {
                /* Fast path: if `a` is a mutable string exclusively owned on the stack,
                 * append b directly into it — zero allocation. */
                ObjString* sa = AS_STRING(a);
                ObjString* sb = AS_STRING(b);
                if (sa->is_mutable && sa->obj.refcount == 1) {
                    obj_string_append(sa, sb->chars, sb->length);
                    vm->stack_top--; /* pop b, leave a (now extended) on stack */
                } else {
                    Value res = OBJ_VAL(obj_string_concat(sa, sb));
                    vm->stack_top -= 2;
                    vm_push(vm, res);
                }
            } else if (IS_STRING(a) || IS_STRING(b)) {
                /* Auto-convert non-string side — separate bufs, no shared state */
                char bufa[64], bufb[64];
                ObjString* sa;
                ObjString* sb;
                if (!IS_STRING(a)) {
                    if (IS_INT(a)) snprintf(bufa, sizeof(bufa), "%lld", (long long)AS_INT(a));
                    else if (IS_FLOAT(a)) {
                        snprintf(bufa, sizeof(bufa), "%.10f", AS_FLOAT(a));
                        char* dot = strchr(bufa, '.');
                        if (dot) { char* end = bufa + strlen(bufa) - 1; while (end > dot + 1 && *end == '0') end--; *(end + 1) = '\0'; }
                    }
                    else if (IS_BOOL(a)) snprintf(bufa, sizeof(bufa), "%s", AS_BOOL(a) ? "true" : "false");
                    else snprintf(bufa, sizeof(bufa), "null");
                    sa = obj_string_new(bufa, (int)strlen(bufa));
                    sa->is_mutable = true; /* new temp string, safe to mutate */
                } else {
                    sa = AS_STRING(a);
                }
                if (!IS_STRING(b)) {
                    if (IS_INT(b)) snprintf(bufb, sizeof(bufb), "%lld", (long long)AS_INT(b));
                    else if (IS_FLOAT(b)) {
                        snprintf(bufb, sizeof(bufb), "%.10f", AS_FLOAT(b));
                        char* dot = strchr(bufb, '.');
                        if (dot) { char* end = bufb + strlen(bufb) - 1; while (end > dot + 1 && *end == '0') end--; *(end + 1) = '\0'; }
                    }
                    else if (IS_BOOL(b)) snprintf(bufb, sizeof(bufb), "%s", AS_BOOL(b) ? "true" : "false");
                    else snprintf(bufb, sizeof(bufb), "null");
                    sb = obj_string_new(bufb, (int)strlen(bufb));
                } else {
                    sb = AS_STRING(b);
                }
                Value res = OBJ_VAL(obj_string_concat(sa, sb));
                vm->stack_top -= 2;
                vm_push(vm, res);
            } else if ((IS_LIST(a) || IS_RANGE(a)) && (IS_LIST(b) || IS_RANGE(b))) {
                ObjList* result = obj_list_new();
                result->obj.is_manual = true;
                
                // Append from a
                if (IS_LIST(a)) {
                    ObjList* la = AS_LIST(a);
                    for (int i = 0; i < la->count; i++) obj_list_append(result, la->items[i]);
                } else {
                    ObjRange* ra = AS_RANGE(a);
                    for (int64_t i = 0; i < ra->length; i++) {
                        obj_list_append(result, INT_VAL(ra->start + i * ra->step));
                    }
                }

                // Append from b
                if (IS_LIST(b)) {
                    ObjList* lb = AS_LIST(b);
                    for (int i = 0; i < lb->count; i++) obj_list_append(result, lb->items[i]);
                } else {
                    ObjRange* rb = AS_RANGE(b);
                    for (int64_t i = 0; i < rb->length; i++) {
                        obj_list_append(result, INT_VAL(rb->start + i * rb->step));
                    }
                }
                
                result->obj.is_manual = false;
                vm->stack_top -= 2;
                vm_push(vm, OBJ_VAL(result));
            } else {
                Value res = num_add(a, b);
                vm->stack_top -= 2;
                vm_push(vm, res);
            }
        } break;
        case OP_SUB: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, num_sub(a, b)); } break;
        case OP_MUL: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, num_mul(a, b)); } break;
        case OP_DIV: {
            Value b = vm_pop(vm), a = vm_pop(vm);
            Value res;
            if (!num_div(vm, a, b, &res)) return INTERPRET_RUNTIME_ERROR;
            vm_push(vm, res);
        } break;
        case OP_MOD: {
            Value b = vm_pop(vm), a = vm_pop(vm);
            Value res;
            if (!num_mod(vm, a, b, &res)) return INTERPRET_RUNTIME_ERROR;
            vm_push(vm, res);
        } break;
        case OP_NEGATE: {
            Value v = vm_pop(vm);
            if (IS_INT(v)) vm_push(vm, INT_VAL(-AS_INT(v)));
            else vm_push(vm, FLOAT_VAL(-value_as_number(v)));
        } break;

        case OP_EQ:  { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(value_equal(a, b))); } break;
        case OP_NEQ: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(!value_equal(a, b))); } break;
        case OP_LT:  { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(value_as_number(a) < value_as_number(b))); } break;
        case OP_GT:  { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(value_as_number(a) > value_as_number(b))); } break;
        case OP_LTE: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(value_as_number(a) <= value_as_number(b))); } break;
        case OP_GTE: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, BOOL_VAL(value_as_number(a) >= value_as_number(b))); } break;
        case OP_NOT: {
            Value v = vm_pop(vm);
            bool err = false;
            bool falsy = is_falsy(vm, v, &err);
            if (err) {
                if (!vm_runtime_error(vm, "Condition must be a boolean, got %s.", value_type_name(v)))
                    return INTERPRET_RUNTIME_ERROR;
            } else {
                vm_push(vm, BOOL_VAL(falsy));
            }
        } break;

        case OP_POP: vm_pop(vm); break;

        case OP_GET_LOCAL:  { uint8_t slot = READ_BYTE(); vm_push(vm, frame->slots[slot]); } break;
        case OP_SET_LOCAL:  { 
            uint8_t slot = READ_BYTE(); 
            Value val = vm_peek(vm, 0);
            /* Layer 2 runtime escape: check if assigned to variable outside current scope */
            if (IS_POINTER(val)) {
                if (vm->scope_depth > 0 && vm->scope_base_slots[vm->scope_depth]) {
                    Value* slot_ptr = &frame->slots[slot];
                    if (slot_ptr < vm->scope_base_slots[vm->scope_depth]) {
                        AS_POINTER(val)->escaped = true;
                    }
                }
            }
            if (IS_LIST(val)) {
                if (vm->scope_depth > 0 && vm->scope_base_slots[vm->scope_depth]) {
                    Value* slot_ptr = &frame->slots[slot];
                    if (slot_ptr < vm->scope_base_slots[vm->scope_depth]) {
                        AS_LIST(val)->escaped = true;
                    }
                }
            }
            if (IS_MAP(val)) {
                if (vm->scope_depth > 0 && vm->scope_base_slots[vm->scope_depth]) {
                    Value* slot_ptr = &frame->slots[slot];
                    if (slot_ptr < vm->scope_base_slots[vm->scope_depth]) {
                        AS_MAP(val)->escaped = true;
                    }
                }
            }
            frame->slots[slot] = val; 
        } break;
        case OP_GET_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value val;
            if (!table_get(&vm->globals, name, &val)) {
                if (!vm_runtime_error(vm, "Undefined variable '%s'.", name->chars))
                    return INTERPRET_RUNTIME_ERROR;
            } else {
                vm_push(vm, val);
            }
        } break;
        case OP_SET_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value val = vm_peek(vm, 0);
            if (IS_POINTER(val)) AS_POINTER(val)->escaped = true;
            if (IS_LIST(val)) AS_LIST(val)->escaped = true;
            if (IS_MAP(val)) AS_MAP(val)->escaped = true;
            table_set(&vm->globals, name, val);
        } break;
        case OP_DEFINE_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value val = vm_peek(vm, 0);
            if (IS_POINTER(val)) AS_POINTER(val)->escaped = true;
            if (IS_LIST(val)) AS_LIST(val)->escaped = true;
            if (IS_MAP(val)) AS_MAP(val)->escaped = true;
            table_set(&vm->globals, name, val);
            vm_pop(vm);
        } break;

        case OP_JUMP:          { uint16_t off = READ_SHORT(); frame->ip += off; } break;
        case OP_JUMP_IF_FALSE: {
            uint16_t off = READ_SHORT();
            bool err = false;
            bool falsy = is_falsy(vm, vm_peek(vm, 0), &err);
            if (err) {
                if (!vm_runtime_error(vm, "Condition must be a boolean, got %s.", value_type_name(vm_peek(vm, 0))))
                    return INTERPRET_RUNTIME_ERROR;
            } else {
                if (falsy) frame->ip += off;
            }
        } break;
        case OP_LOOP:          { uint16_t off = READ_SHORT(); frame->ip -= off; } break;

        case OP_CALL: {
            int argc = READ_BYTE();
            Value callee = vm_peek(vm, argc);
            /* Fast path: inline native calls — no frame allocation, no escape scan */
            if (IS_NATIVE(callee)) {
                Value result = AS_NATIVE(callee)->function(vm, argc, vm->stack_top - argc);
                vm->stack_top -= argc + 1;
                vm_push(vm, result);
                break;
            }
            /* Escape scan only needed when passing pointers */
            for (int i = 0; i < argc; i++) {
                Value arg = vm_peek(vm, i);
                if (IS_POINTER(arg)) AS_POINTER(arg)->escaped = true;
                if (IS_LIST(arg))    AS_LIST(arg)->escaped = true;
                if (IS_MAP(arg))     AS_MAP(arg)->escaped = true;
            }
            if (!call_value(vm, callee, argc)) return INTERPRET_RUNTIME_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
        } break;

        case OP_RETURN: {
            Value result = vm_pop(vm);
            if (IS_POINTER(result)) AS_POINTER(result)->escaped = true;
            if (IS_LIST(result))    AS_LIST(result)->escaped = true;
            if (IS_MAP(result))     AS_MAP(result)->escaped = true;
            int restore_depth = frame->saved_scope_depth;
            
            /* Unwind all scopes created within this function */
            while (vm->scope_depth > restore_depth) {
                vm_exit_scope(vm);
            }

            vm->frame_count--;
            if (vm->frame_count == 0) { vm_pop(vm); return INTERPRET_OK; }
            vm->stack_top = frame->slots;
            vm->scope_depth = restore_depth; /* restore caller's scope depth (should already be reached) */
            vm_push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
        } break;

        case OP_LIST_NEW: {
            int count = READ_BYTE();
            ObjList* list = obj_list_new();
            list->obj.is_manual = true;
            for (int i = count - 1; i >= 0; i--)
                obj_list_append(list, vm_peek(vm, i));
            list->obj.is_manual = false;
            list->scope_depth = vm->scope_depth;
            list->auto_manage = global_autofree;
            list->escaped = false;
            vm->stack_top -= count;
            vm_push(vm, OBJ_VAL(list));
        } break;

        case OP_MAP_NEW: {
            int count = READ_BYTE();
            ObjMap* map = obj_map_new();
            map->obj.is_manual = true;
            for (int i = count - 1; i >= 0; i--) {
                Value val = vm_peek(vm, i * 2);
                Value key = vm_peek(vm, i * 2 + 1);
                obj_map_set(map, key, val);
            }
            map->obj.is_manual = false;
            map->scope_depth = vm->scope_depth;
            map->auto_manage = global_autofree;
            map->escaped = false;
            vm->stack_top -= count * 2;
            vm_push(vm, OBJ_VAL(map));
        } break;

        case OP_INDEX_GET: {
    Value idx = vm_peek(vm, 0);
    Value obj = vm_peek(vm, 1);
    Value result = NULL_VAL;
    if (IS_RANGE(obj) && IS_INT(idx)) {
        ObjRange* r = (ObjRange*)AS_OBJ(obj);
        int64_t i = AS_INT(idx);
        if (i >= 0 && i < r->length) {
            result = INT_VAL(r->start + i * r->step);
        }
    } else if (IS_LIST(obj) && IS_INT(idx)) {
        ObjList* l = AS_LIST(obj);
        int i = (int)AS_INT(idx);
        if (i < 0 || i >= l->count) { result = NULL_VAL; }
        else result = l->items[i];
    } else if (IS_STRING(obj) && IS_INT(idx)) {
        ObjString* s = AS_STRING(obj);
        int i = (int)AS_INT(idx);
        if (i < 0 || i >= s->length) { result = NULL_VAL; }
        else result = OBJ_VAL(obj_string_new(&s->chars[i], 1));
    } else if (IS_MAP(obj)) {
        // Special case for our internal for-in loop which currently yields index integers
        if (IS_INT(idx) && AS_INT(idx) >= 0 && AS_INT(idx) < AS_MAP(obj)->capacity) {
             // Wait, the previous implementation used `idx` as "nth occupied element"
             ObjMap* map = AS_MAP(obj);
             int n = (int)AS_INT(idx);
             int found = 0;
             for (int mi = 0; mi < map->capacity; mi++) {
                 if (map->entries[mi].occupied) {
                     if (found == n) {
                         result = map->entries[mi].key;
                         goto index_get_done;
                     }
                     found++;
                 }
             }
             // If not found as Nth element, we still proceed to map lookup just in case
        }
        
        if (!obj_map_get(AS_MAP(obj), idx, &result)) result = NULL_VAL;
    } else {
        vm_runtime_error(vm, "Cannot index %s with %s.", value_type_name(obj), value_type_name(idx));
        return INTERPRET_RUNTIME_ERROR;
    }
    index_get_done:
    vm->stack_top -= 2;
    vm_push(vm, result);
} break;

        case OP_INDEX_SET: {
            Value val = vm_peek(vm, 0);
            Value idx = vm_peek(vm, 1);
            Value obj = vm_peek(vm, 2);
            if (IS_POINTER(val)) AS_POINTER(val)->escaped = true;
            if (IS_LIST(val))    AS_LIST(val)->escaped = true;
            if (IS_MAP(val))     AS_MAP(val)->escaped = true;
            if (IS_LIST(obj) && IS_INT(idx)) {
                ObjList* l = AS_LIST(obj);
                int i = (int)AS_INT(idx);
                if (i >= 0 && i < l->count) l->items[i] = val;
            } else if (IS_MAP(obj)) {
                obj_map_set(AS_MAP(obj), idx, val);
            }
            vm->stack_top[-3] = val;
            vm->stack_top -= 2;
        } break;

        case OP_PRINT: { value_print(vm_pop(vm)); printf("\n"); } break;

        case OP_LEN: {
            Value v = vm_pop(vm);
            if (IS_STRING(v)) vm_push(vm, INT_VAL(AS_STRING(v)->length));
            else if (IS_LIST(v)) vm_push(vm, INT_VAL(AS_LIST(v)->count));
            else if (IS_MAP(v)) vm_push(vm, INT_VAL(AS_MAP(v)->count));
            else if (IS_RANGE(v)) vm_push(vm, INT_VAL(range_length(AS_RANGE(v))));
            else vm_push(vm, INT_VAL(0));
        } break;

        case OP_ENTER_SCOPE: {
            if (vm->scope_depth < MAX_LOCAL_SCOPES) {
                /* We record the marker at scope_depth BEFORE incrementing.
                 * vm_exit_scope() decrements scope_depth first, then reads
                 * scope_alloc_markers[vm->scope_depth].  So the index used
                 * on exit equals the index we store here. */
                vm->scope_base_slots[vm->scope_depth] = vm->stack_top;
                extern Obj* all_objects;
                vm->scope_alloc_markers[vm->scope_depth] = all_objects;
            }
            vm->scope_depth++;
        } break;
        
        case OP_EXIT_SCOPE: {
            vm_exit_scope(vm);
        } break;
        
        case OP_MARK_ESCAPED: {
            Value v = vm_peek(vm, 0);
            if (IS_POINTER(v)) AS_POINTER(v)->escaped = true;
        } break;

        case OP_ALLOC: {
            Value v = vm_peek(vm, 0);
            /* Wrap in a pointer — manual memory */
            Value* heap = (Value*)malloc(sizeof(Value));
            *heap = v;
            ObjPointer* ptr = obj_pointer_new(heap);
            ptr->obj.is_manual = true;
            ptr->alloc_size = sizeof(ObjPointer) + sizeof(Value);
            
            ObjString* type_str = AS_STRING(READ_CONSTANT());
            ptr->alloc_type = type_str;
            ptr->auto_manage = (READ_BYTE() == 1);
            ptr->alloc_func = frame->function->name;
            int offset = (int)(frame->ip - frame->function->chunk->code - 4); // -4: 2-byte constant index + 1 auto_manage byte, back to the OP_ALLOC itself
            ptr->alloc_line = frame->function->chunk->lines[offset];
            ptr->scope_depth = vm->scope_depth;
            ptr->escaped = false;
            
            vm->stack_top -= 1;
            vm_push(vm, OBJ_VAL(ptr));
        } break;

        case OP_FREE: {
            Value v = vm_pop(vm);
            if (IS_POINTER(v)) {
                ObjPointer* p = AS_POINTER(v);
                if (p->is_valid && p->target) { 
                    free(p->target); 
                    p->target = nullptr; 
                    p->is_valid = false; 
                    
                    /* Physically Unlink ObjPointer Structure from Linked List */
                    extern Obj* all_objects;
                    Obj* o = all_objects;
                    Obj* prev = nullptr;
                    while (o) {
                        if (o == (Obj*)p) {
                            if (prev == nullptr) all_objects = o->next;
                            else prev->next = o->next;
                            
                            extern void* tantrums_realloc(void* pointer, size_t oldSize, size_t newSize);
                            tantrums_realloc(o, sizeof(ObjPointer), 0);
                            break;
                        }
                        prev = o;
                        o = o->next;
                    }
                } else {
                    vm_runtime_error(vm, "Double-free detected: pointer has already been freed.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
        } break;

        case OP_PTR_REF: {
            /* For now, can only take address of locals via slot */
            /* Simplified: wrap the value in a pointer */
            Value v = vm_peek(vm, 0);
            /* Point to the actual stack slot */
            Value* target = vm->stack_top - 1;
            ObjPointer* ptr = obj_pointer_new(target);
            vm_pop(vm);
            vm_push(vm, OBJ_VAL(ptr));
        } break;

        case OP_PTR_DEREF: {
            Value v = vm_pop(vm);
            if (!IS_POINTER(v)) {
                if (!vm_runtime_error(vm, "Cannot dereference a non-pointer (got %s).", value_type_name(v)))
                    return INTERPRET_RUNTIME_ERROR;
                break;
            }
            ObjPointer* p = AS_POINTER(v);
            if (!p->is_valid || !p->target) {
                const char* type_name = p->alloc_type ? p->alloc_type->chars : "dynamic";
                if (!vm_runtime_error(vm, "Null pointer dereference on %s* pointer!", type_name))
                    return INTERPRET_RUNTIME_ERROR;
                break;
            }
            vm_push(vm, *p->target);
        } break;

        case OP_PTR_SET: {
            Value ptr_val = vm_pop(vm);
            Value new_val = vm_peek(vm, 0);
            if (!IS_POINTER(ptr_val)) {
                vm_runtime_error(vm, "Cannot dereference a non-pointer for assignment.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjPointer* p = AS_POINTER(ptr_val);
            if (!p->is_valid || !p->target) {
                vm_runtime_error(vm, "Null pointer dereference on assignment!");
                return INTERPRET_RUNTIME_ERROR;
            }
            *p->target = new_val;
        } break;

        case OP_CAST: {
            uint8_t tag = READ_BYTE(); /* 0=int, 1=float, 2=string, 3=bool */
            Value v = vm_peek(vm, 0);
            Value result = NULL_VAL;
            switch (tag) {
            case 0: /* cast to int */ {
                if (IS_INT(v)) { result = v; }
                else if (IS_FLOAT(v)) { result = INT_VAL((int64_t)AS_FLOAT(v)); }
                else if (IS_BOOL(v)) { result = INT_VAL(AS_BOOL(v) ? 1 : 0); }
                else if (IS_STRING(v)) {
                    char* end = nullptr;
                    int64_t n = strtoll(AS_CSTRING(v), &end, 10);
                    result = INT_VAL(n);
                }
                else result = INT_VAL(0);
            } break;
            case 1: /* cast to float */ {
                if (IS_FLOAT(v)) { result = v; }
                else if (IS_INT(v)) { result = FLOAT_VAL((double)AS_INT(v)); }
                else if (IS_STRING(v)) {
                    double d = strtod(AS_CSTRING(v), nullptr);
                    result = FLOAT_VAL(d);
                }
                else result = FLOAT_VAL(0.0);
            } break;
            case 2: /* cast to string */ {
                if (IS_STRING(v)) { result = v; }
                else if (IS_INT(v)) {
                    char buf[64]; snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(v));
                    result = OBJ_VAL(obj_string_new(buf, (int)strlen(buf)));
                }
                else if (IS_FLOAT(v)) {
                    char buf[64]; 
                    snprintf(buf, sizeof(buf), "%.10f", AS_FLOAT(v));
                    char* dot = strchr(buf, '.');
                    if (dot) { char* end = buf + strlen(buf) - 1; while (end > dot + 1 && *end == '0') end--; *(end + 1) = '\0'; }
                    result = OBJ_VAL(obj_string_new(buf, (int)strlen(buf)));
                }
                else if (IS_BOOL(v)) {
                    const char* s = AS_BOOL(v) ? "true" : "false";
                    result = OBJ_VAL(obj_string_new(s, (int)strlen(s)));
                }
                else result = OBJ_VAL(obj_string_new("null", 4));
            } break;
            case 3: /* cast to bool */ {
                if (IS_BOOL(v)) { result = v; }
                else if (IS_NULL(v)) { result = BOOL_VAL(false); }
                else if (IS_INT(v)) { result = BOOL_VAL(AS_INT(v) != 0); }
                else if (IS_STRING(v)) {
                    ObjString* s = AS_STRING(v);
                    if (s->length == 4 && memcmp(s->chars, "true", 4) == 0) {
                        result = BOOL_VAL(true);
                    } else if (s->length == 5 && memcmp(s->chars, "false", 5) == 0) {
                        result = BOOL_VAL(false);
                    } else {
                        bool has_content = false;
                        for (int ci = 0; ci < s->length; ci++) {
                            char ch = s->chars[ci];
                            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                                has_content = true; break;
                            }
                        }
                        result = BOOL_VAL(has_content);
                    }
                }
                else result = BOOL_VAL(true);
            } break;
            default: result = v; break;
            }
            vm->stack_top -= 1;
            vm_push(vm, result);
        } break;

        case OP_CLONE: {
            Value v = vm_peek(vm, 0);
            if (IS_LIST(v)) {
                vm->stack_top -= 1;
                vm_push(vm, OBJ_VAL(obj_list_clone(AS_LIST(v))));
            } else {
                // For now, only lists are cloneable. Other types are copied by value or reference.
                // If we want to clone other types, we'd add more cases here.
                // For example, for maps:
                // else if (IS_MAP(v)) {
                //     vm->stack_top -= 1;
                //     vm_push(vm, OBJ_VAL(obj_map_clone(AS_MAP(v))));
                // }
                // For now, if not a list, just push the original value back (effectively a no-op for non-cloneable types)
                // Or, raise a runtime error if cloning is strictly defined for specific types.
                // For simplicity, we'll just pop and push the original value back, making it a no-op for non-lists.
                // This means `clone(x)` where `x` is not a list will just return `x`.
                // If we want to enforce cloneability, we'd do:
                // vm_runtime_error(vm, "Cannot clone type %s.", value_type_name(v));
                // return INTERPRET_RUNTIME_ERROR;
            }
        } break;

        case OP_THROW: {
            Value v = vm_pop(vm);
            /* Check if there's an exception handler */
            if (vm->handler_count > 0) {
                ExceptionHandler* h = &vm->handlers[--vm->handler_count];
                /* Unwind stack and frames */
                vm->stack_top = h->stack_top;
                vm->frame_count = h->frame_count;
                frame = &vm->frames[vm->frame_count - 1];
                frame->ip = h->catch_ip;
                /* Push the error value for catch block */
                vm_push(vm, v);
            } else {
                /* No handler — fatal error */
                fprintf(stderr, "\n[Tantrums Error] ");
                value_print(v);
                fprintf(stderr, "\n");
                return INTERPRET_RUNTIME_ERROR;
            }
        } break;

        case OP_TRY_BEGIN: {
            uint16_t offset = READ_SHORT();
            if (vm->handler_count >= MAX_EXCEPTION_HANDLERS) {
                vm_runtime_error(vm, "Too many nested try blocks.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ExceptionHandler* h = &vm->handlers[vm->handler_count++];
            h->catch_ip = frame->ip + offset;  /* jump target for catch */
            h->frame_count = vm->frame_count;
            h->stack_top = vm->stack_top;
        } break;

        case OP_TRY_END: {
            /* Try block completed normally — pop handler */
            if (vm->handler_count > 0) vm->handler_count--;
        } break;

        case OP_FOR_IN_STEP: {
            uint8_t iter_slot = READ_BYTE();
            uint8_t length_slot = READ_BYTE();
            uint8_t counter_slot = READ_BYTE();
            
            Value iter_val = frame->slots[iter_slot];
            int64_t counter = AS_INT(frame->slots[counter_slot]);
            int64_t length = AS_INT(frame->slots[length_slot]);
            
            if (counter < length) {
                Value element;
                if (IS_RANGE(iter_val)) {
                    ObjRange* r = AS_RANGE(iter_val);
                    element = INT_VAL(r->start + counter * r->step);
                } else if (IS_LIST(iter_val)) {
                    element = AS_LIST(iter_val)->items[counter];
                } else if (IS_STRING(iter_val)) {
                    element = OBJ_VAL(obj_string_new(&(AS_STRING(iter_val)->chars[counter]), 1));
                } else {
                    element = NULL_VAL;
                }
                
                frame->slots[counter_slot] = INT_VAL(counter + 1);
                vm_push(vm, element);          // Loop variable value
                vm_push(vm, BOOL_VAL(true));  // Continue bool for JUMP_IF_FALSE
            } else {
                vm_push(vm, BOOL_VAL(false)); // Exit bool for JUMP_IF_FALSE
            }
            break;
        }

        case OP_FREE_COLLECTION: {
            /* Compile-time confirmed non-escaping list/map — free its heap buffer silently */
            Value v = vm_pop(vm);
            if (IS_LIST(v)) {
                ObjList* lst = AS_LIST(v);
                if (lst->items) {
                    free(lst->items);
                    lst->items = nullptr;
                    lst->count = 0;
                    lst->capacity = 0;
                }
                /* Unlink from all_objects */
                extern Obj* all_objects;
                Obj* o = all_objects; Obj* prev = nullptr;
                while (o) {
                    if (o == (Obj*)lst) {
                        if (prev == nullptr) all_objects = o->next;
                        else prev->next = o->next;
                        tantrums_realloc(o, sizeof(ObjList), 0);
                        break;
                    }
                    prev = o; o = o->next;
                }
            } else if (IS_MAP(v)) {
                ObjMap* mp = AS_MAP(v);
                if (mp->entries) {
                    free(mp->entries);
                    mp->entries = nullptr;
                    mp->count = 0;
                    mp->capacity = 0;
                }
                extern Obj* all_objects;
                Obj* o = all_objects; Obj* prev = nullptr;
                while (o) {
                    if (o == (Obj*)mp) {
                        if (prev == nullptr) all_objects = o->next;
                        else prev->next = o->next;
                        tantrums_realloc(o, sizeof(ObjMap), 0);
                        break;
                    }
                    prev = o; o = o->next;
                }
            }
        } break;

        case OP_HALT: {
            while (vm->scope_depth > 0) vm_exit_scope(vm);
            return INTERPRET_OK;
        }

        default:
            vm_runtime_error(vm, "Unknown opcode %d.", instruction);
            return INTERPRET_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
}

InterpretResult vm_interpret(VM* vm, const char* source) {
    /* Lex */
    Lexer lexer;
    lexer_init(&lexer, source);
    TokenList tokens = lexer_scan_tokens(&lexer);

    /* Check for lexer errors */
    for (int i = 0; i < tokens.count; i++) {
        if (tokens.tokens[i].type == TOKEN_ERROR) {
            fprintf(stderr, "[Line %d] Lexer error: %.*s\n",
                    tokens.tokens[i].line, tokens.tokens[i].length, tokens.tokens[i].start);
            tokenlist_free(&tokens);
            return INTERPRET_COMPILE_ERROR;
        }
    }

    /* Parse */
    ASTNode* ast = parser_parse(&tokens);
    tokenlist_free(&tokens);
    if (!ast) return INTERPRET_COMPILE_ERROR;

    /* Resolve imports: read, parse, and merge imported files */
    if (ast->type == NODE_PROGRAM) {
        /* Collect already-imported files to avoid duplicates */
        static const int MAX_IMPORTS = 64;
        char* imported[MAX_IMPORTS];
        int import_count = 0;

        /* Walk program nodes and process NODE_USE */
        for (int i = 0; i < ast->as.program.count; i++) {
            ASTNode* n = ast->as.program.nodes[i];
            if (n->type != NODE_USE) continue;

            const char* filename = n->as.use_file;

            /* Check for duplicate import */
            bool already = false;
            for (int j = 0; j < import_count; j++) {
                if (strcmp(imported[j], filename) == 0) { already = true; break; }
            }
            if (already) continue;

            /* Track this import */
            if (import_count < MAX_IMPORTS) {
                imported[import_count] = (char*)malloc(strlen(filename) + 1);
                strcpy(imported[import_count], filename);
                import_count++;
            }

            /* Read the imported file */
            FILE* f = fopen(filename, "rb");
            if (!f) {
                fprintf(stderr, "[Line %d] Import Error: Cannot find '%s' in current directory.\n",
                        n->line, filename);
                /* Cleanup */
                for (int j = 0; j < import_count; j++) free(imported[j]);
                ast_free(ast);
                return INTERPRET_COMPILE_ERROR;
            }
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* file_source = (char*)malloc(size + 1);
            fread(file_source, 1, size, f);
            file_source[size] = '\0';
            fclose(f);

            /* Lex and parse the imported file */
            Lexer imp_lexer;
            lexer_init(&imp_lexer, file_source);
            TokenList imp_tokens = lexer_scan_tokens(&imp_lexer);
            ASTNode* imp_ast = parser_parse(&imp_tokens);

            if (!imp_ast) {
                fprintf(stderr, "[Line %d] Import Error: Failed to parse '%s'.\n", n->line, filename);
                tokenlist_free(&imp_tokens);
                free(file_source);
                for (int j = 0; j < import_count; j++) free(imported[j]);
                ast_free(ast);
                return INTERPRET_COMPILE_ERROR;
            }

            printf("[Tantrums] Imported '%s' (%d declarations)\n", filename, imp_ast->as.program.count);

            /* Inject imported declarations at position i (replacing the USE node) */
            int inj_count = imp_ast->as.program.count;
            if (inj_count > 0) {
                /* Make room: expand the program's node list */
                int old_count = ast->as.program.count;
                int new_count = old_count - 1 + inj_count; /* -1 for removing USE, +inj for imports */
                /* Ensure capacity */
                while (ast->as.program.capacity < new_count) {
                    int cap = ast->as.program.capacity < 8 ? 8 : ast->as.program.capacity * 2;
                    ast->as.program.nodes = (ASTNode**)realloc(ast->as.program.nodes, sizeof(ASTNode*) * cap);
                    ast->as.program.capacity = cap;
                }
                /* Shift nodes after i to make room */
                if (inj_count > 1) {
                    memmove(&ast->as.program.nodes[i + inj_count],
                            &ast->as.program.nodes[i + 1],
                            sizeof(ASTNode*) * (old_count - i - 1));
                } else if (inj_count == 0) {
                    memmove(&ast->as.program.nodes[i],
                            &ast->as.program.nodes[i + 1],
                            sizeof(ASTNode*) * (old_count - i - 1));
                }
                /* Copy imported nodes into position */
                for (int k = 0; k < inj_count; k++) {
                    ast->as.program.nodes[i + k] = imp_ast->as.program.nodes[k];
                    imp_ast->as.program.nodes[k] = nullptr; /* prevent double-free */
                }
                ast->as.program.count = new_count;
                /* Free the USE node */
                ast_free(n);
                /* Adjust i to skip past injected nodes */
                i += inj_count - 1;
            } else {
                /* No declarations in import — just remove the USE node */
                memmove(&ast->as.program.nodes[i],
                        &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (ast->as.program.count - i - 1));
                ast->as.program.count--;
                ast_free(n);
                i--;
            }

            /* Free imp_ast shell (nodes transferred) */
            free(imp_ast->as.program.nodes);
            free(imp_ast);
            /* Keep file_source alive — tokens reference it
               Actually tokenlist was freed, but AST nodes may reference it via string copies.
               The parser's copy_lexeme allocates fresh strings, so we can free it. */
            tokenlist_free(&imp_tokens);
            free(file_source);
        }

        /* Cleanup import tracking */
        for (int j = 0; j < import_count; j++) free(imported[j]);
    }

    /* Compile */
    ObjFunction* script = compiler_compile(ast, MODE_BOTH);
    ast_free(ast);
    if (!script) return INTERPRET_COMPILE_ERROR;

    /* Set up VM to run the script */
    current_vm_for_gc = vm;
    vm_push(vm, OBJ_VAL(script));
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->function = script;
    frame->ip = script->chunk->code;
    frame->slots = vm->stack;
    frame->saved_scope_depth = 0;

    /* Run the top-level code (defines functions/globals) */
    InterpretResult result = run(vm);
    if (result != INTERPRET_OK) return result;

    /* Look for and call main() */
    ObjString* main_name = obj_string_new("main", 4);
    Value main_fn;
    if (table_get(&vm->globals, main_name, &main_fn) && IS_FUNCTION(main_fn)) {
        vm_push(vm, main_fn);
        if (!call_value(vm, main_fn, 0)) return INTERPRET_RUNTIME_ERROR;
        return run(vm);
    }

    /* No main() found — that's fine, top-level code already ran */
    return INTERPRET_OK;
}

InterpretResult vm_interpret_compiled(VM* vm, ObjFunction* script) {
    /* Set up the script in the VM */
    current_vm_for_gc = vm;
    vm_push(vm, OBJ_VAL(script));
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->function = script;
    frame->ip = script->chunk->code;
    frame->slots = vm->stack;

    /* Run top-level code (defines functions/globals) */
    InterpretResult result = run(vm);
    if (result != INTERPRET_OK) return result;

    /* Look for and call main() */
    ObjString* main_name = obj_string_new("main", 4);
    Value main_fn;
    if (table_get(&vm->globals, main_name, &main_fn) && IS_FUNCTION(main_fn)) {
        vm_push(vm, main_fn);
        if (!call_value(vm, main_fn, 0)) return INTERPRET_RUNTIME_ERROR;
        return run(vm);
    }

    return INTERPRET_OK;
}