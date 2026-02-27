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

void vm_init(VM* vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->handler_count = 0;
    vm->objects = nullptr;
    table_init(&vm->globals);
    builtins_register(vm);
}

void vm_free(VM* vm) {
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

void vm_runtime_error(VM* vm, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\n[Tantrums Runtime Error] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* fn = frame->function;
        int offset = (int)(frame->ip - fn->chunk->code - 1);
        int line = fn->chunk->lines[offset];
        fprintf(stderr, "  [line %d] in %s\n", line, fn->name ? fn->name->chars : "<script>");
    }
}

static bool is_falsy(Value v) {
    if (IS_NULL(v)) return true;
    if (IS_BOOL(v)) return !AS_BOOL(v);
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
static Value num_div(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) {
        if (AS_INT(b) == 0) return INT_VAL(0);
        return INT_VAL(AS_INT(a) / AS_INT(b));
    }
    double d = value_as_number(b);
    if (d == 0) return FLOAT_VAL(0);
    return FLOAT_VAL(value_as_number(a) / d);
}
static Value num_mod(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) {
        if (AS_INT(b) == 0) return INT_VAL(0);
        return INT_VAL(AS_INT(a) % AS_INT(b));
    }
    return INT_VAL(0);
}

static bool call_value(VM* vm, Value callee, int argc) {
    if (IS_FUNCTION(callee)) {
        ObjFunction* fn = AS_FUNCTION(callee);
        if (argc != fn->arity) {
            vm_runtime_error(vm, "'%s' expected %d args but got %d.", fn->name ? fn->name->chars : "?", fn->arity, argc);
            return false;
        }
        if (vm->frame_count >= MAX_FRAMES) {
            vm_runtime_error(vm, "Stack overflow (too many calls).");
            return false;
        }
        CallFrame* frame = &vm->frames[vm->frame_count++];
        frame->function = fn;
        frame->ip = fn->chunk->code;
        frame->slots = vm->stack_top - argc - 1;
        return true;
    }
    if (IS_NATIVE(callee)) {
        ObjNative* native = AS_NATIVE(callee);
        Value result = native->function(vm, argc, vm->stack_top - argc);
        vm->stack_top -= argc + 1;
        vm_push(vm, result);
        return true;
    }
    vm_runtime_error(vm, "Can only call functions.");
    return false;
}

static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk->constants[READ_BYTE()])

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
                Value res = OBJ_VAL(obj_string_concat(AS_STRING(a), AS_STRING(b)));
                vm->stack_top -= 2;
                vm_push(vm, res);
            } else if (IS_STRING(a) || IS_STRING(b)) {
                /* Auto-convert non-string side to string */
                char buf[128];
                ObjString* sa = nullptr;
                ObjString* sb = nullptr;
                if (!IS_STRING(a)) {
                    if (IS_INT(a)) snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(a));
                    else if (IS_FLOAT(a)) snprintf(buf, sizeof(buf), "%g", AS_FLOAT(a));
                    else if (IS_BOOL(a)) snprintf(buf, sizeof(buf), "%s", AS_BOOL(a) ? "true" : "false");
                    else snprintf(buf, sizeof(buf), "null");
                    sa = obj_string_new(buf, (int)strlen(buf));
                    sa->obj.is_manual = true; // Protect sa
                } else {
                    sa = AS_STRING(a);
                }
                
                if (!IS_STRING(b)) {
                    if (IS_INT(b)) snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(b));
                    else if (IS_FLOAT(b)) snprintf(buf, sizeof(buf), "%g", AS_FLOAT(b));
                    else if (IS_BOOL(b)) snprintf(buf, sizeof(buf), "%s", AS_BOOL(b) ? "true" : "false");
                    else snprintf(buf, sizeof(buf), "null");
                    sb = obj_string_new(buf, (int)strlen(buf));
                } else {
                    sb = AS_STRING(b);
                }
                
                Value res = OBJ_VAL(obj_string_concat(sa, sb));
                
                if (!IS_STRING(a)) sa->obj.is_manual = false; // Unprotect sa
                
                vm->stack_top -= 2;
                vm_push(vm, res);
            } else if (IS_LIST(a) && IS_LIST(b)) {
                ObjList* result = obj_list_new(); // result is not on stack yet, protect
                result->obj.is_manual = true;
                ObjList* la = AS_LIST(a); ObjList* lb = AS_LIST(b);
                for (int i = 0; i < la->count; i++) obj_list_append(result, la->items[i]);
                for (int i = 0; i < lb->count; i++) obj_list_append(result, lb->items[i]);
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
        case OP_DIV: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, num_div(a, b)); } break;
        case OP_MOD: { Value b = vm_pop(vm), a = vm_pop(vm); vm_push(vm, num_mod(a, b)); } break;
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
        case OP_NOT: { Value v = vm_pop(vm); vm_push(vm, BOOL_VAL(is_falsy(v))); } break;

        case OP_POP: vm_pop(vm); break;

        case OP_GET_LOCAL:  { uint8_t slot = READ_BYTE(); vm_push(vm, frame->slots[slot]); } break;
        case OP_SET_LOCAL:  { uint8_t slot = READ_BYTE(); frame->slots[slot] = vm_peek(vm, 0); } break;
        case OP_GET_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value val;
            if (!table_get(&vm->globals, name, &val)) {
                vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            vm_push(vm, val);
        } break;
        case OP_SET_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            table_set(&vm->globals, name, vm_peek(vm, 0));
        } break;
        case OP_DEFINE_GLOBAL: {
            ObjString* name = AS_STRING(READ_CONSTANT());
            table_set(&vm->globals, name, vm_peek(vm, 0));
            vm_pop(vm);
        } break;

        case OP_JUMP:          { uint16_t off = READ_SHORT(); frame->ip += off; } break;
        case OP_JUMP_IF_FALSE: { uint16_t off = READ_SHORT(); if (is_falsy(vm_peek(vm, 0))) frame->ip += off; } break;
        case OP_LOOP:          { uint16_t off = READ_SHORT(); frame->ip -= off; } break;

        case OP_CALL: {
            int argc = READ_BYTE();
            if (!call_value(vm, vm_peek(vm, argc), argc)) return INTERPRET_RUNTIME_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
        } break;

        case OP_RETURN: {
            Value result = vm_pop(vm);
            vm->frame_count--;
            if (vm->frame_count == 0) { vm_pop(vm); return INTERPRET_OK; }
            vm->stack_top = frame->slots;
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
                if (IS_STRING(key)) obj_map_set(map, AS_STRING(key), val);
            }
            map->obj.is_manual = false;
            vm->stack_top -= count * 2;
            vm_push(vm, OBJ_VAL(map));
        } break;

        case OP_INDEX_GET: {
            Value idx = vm_peek(vm, 0);
            Value obj = vm_peek(vm, 1);
            Value result = NULL_VAL;
            if (IS_LIST(obj) && IS_INT(idx)) {
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
                ObjString* s_idx = nullptr;
                if (IS_STRING(idx)) {
                    s_idx = AS_STRING(idx);
                } else if (IS_INT(idx)) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(idx));
                    s_idx = obj_string_new(buf, (int)strlen(buf));
                }
                if (s_idx) {
                    if (obj_map_get(AS_MAP(obj), s_idx, &result)) {}
                    else result = NULL_VAL;
                } else {
                    vm_runtime_error(vm, "Cannot index %s with %s.", value_type_name(obj), value_type_name(idx));
                    return INTERPRET_RUNTIME_ERROR;
                }
            } else {
                vm_runtime_error(vm, "Cannot index %s with %s.", value_type_name(obj), value_type_name(idx));
                return INTERPRET_RUNTIME_ERROR;
            }
            vm->stack_top -= 2;
            vm_push(vm, result);
        } break;

        case OP_INDEX_SET: {
            Value val = vm_peek(vm, 0);
            Value idx = vm_peek(vm, 1);
            Value obj = vm_peek(vm, 2);
            if (IS_LIST(obj) && IS_INT(idx)) {
                ObjList* l = AS_LIST(obj);
                int i = (int)AS_INT(idx);
                if (i >= 0 && i < l->count) l->items[i] = val;
            } else if (IS_MAP(obj)) {
                ObjString* s_idx = nullptr;
                if (IS_STRING(idx)) {
                    s_idx = AS_STRING(idx);
                } else if (IS_INT(idx)) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(idx));
                    s_idx = obj_string_new(buf, (int)strlen(buf));
                }
                if (s_idx) {
                    obj_map_set(AS_MAP(obj), s_idx, val);
                } else {
                    vm_runtime_error(vm, "Cannot index %s with %s.", value_type_name(obj), value_type_name(idx));
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
            vm->stack_top -= 3;
        } break;

        case OP_PRINT: { value_print(vm_pop(vm)); printf("\n"); } break;

        case OP_LEN: {
            Value v = vm_pop(vm);
            if (IS_STRING(v)) vm_push(vm, INT_VAL(AS_STRING(v)->length));
            else if (IS_LIST(v)) vm_push(vm, INT_VAL(AS_LIST(v)->count));
            else if (IS_MAP(v)) vm_push(vm, INT_VAL(AS_MAP(v)->count));
            else vm_push(vm, INT_VAL(0));
        } break;

        case OP_ALLOC: {
            Value v = vm_peek(vm, 0);
            /* Wrap in a pointer — manual memory */
            Value* heap = (Value*)malloc(sizeof(Value));
            *heap = v;
            ObjPointer* ptr = obj_pointer_new(heap);
            ptr->obj.is_manual = true;
            vm->stack_top -= 1;
            vm_push(vm, OBJ_VAL(ptr));
        } break;

        case OP_FREE: {
            Value v = vm_pop(vm);
            if (IS_POINTER(v)) {
                ObjPointer* p = AS_POINTER(v);
                if (p->is_valid && p->target) { free(p->target); p->target = nullptr; p->is_valid = false; }
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
                vm_runtime_error(vm, "Cannot dereference a non-pointer.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjPointer* p = AS_POINTER(v);
            if (!p->is_valid || !p->target) {
                vm_runtime_error(vm, "Null pointer dereference!");
                return INTERPRET_RUNTIME_ERROR;
            }
            vm_push(vm, *p->target);
        } break;

        case OP_PTR_SET: {
            Value ptr_val = vm_pop(vm);
            Value new_val = vm_pop(vm);
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
                    char buf[64]; snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v));
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

        case OP_HALT: return INTERPRET_OK;

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
