#include "compiler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef struct {
    char name[256];
    int  length;
    int  depth;
    char type_name[32]; /* tracked type, or "" if dynamic */
    bool is_used;       /* tracked usage for unused variable warning */
    bool holds_alloc;   /* tracked pointer allocations for leak errors */
    bool auto_free;     /* naturally local pointer safely auto-freed at end_scope */
} Local;

typedef struct CompilerState {
    ObjFunction*        function;
    struct CompilerState* enclosing;
    Local  locals[MAX_LOCALS];
    int    local_count;
    int    scope_depth;
    char   ret_type[32]; /* current function's return type */
} CompilerState;

static CompilerState* current = nullptr;
static bool had_type_error = false;
static CompileMode compile_mode = MODE_BOTH;
static bool is_in_expr_stmt = false; /* tracking if current expression is a top-level statement */
extern bool suppress_autofree_notes;

static void type_error(int line, const char* msg);

typedef struct Loop {
    struct Loop* enclosing;
    int start;
    int scope_depth;
    int type;  /* 0 = while, 1 = for_in */
    int breaks[64];
    int break_count;
    int continues[64];
    int continue_count;
} Loop;

static Loop* current_loop = nullptr;

/* ── Global Tracking (for detecting duplicate definitions) ── */
#define MAX_GLOBALS 512
static char tracked_globals[MAX_GLOBALS][256];
static int global_count = 0;

static bool is_global_tracked(const char* name) {
    for (int i = 0; i < global_count; i++) {
        if (strcmp(tracked_globals[i], name) == 0) return true;
    }
    return false;
}

static void track_global(const char* name) {
    if (global_count >= MAX_GLOBALS) return;
    strncpy(tracked_globals[global_count++], name, 255);
}

/* ── Function signature table (for type checking calls) ── */
typedef struct {
    char name[256];
    char ret_type[32];
    char param_types[16][32]; /* up to 16 params */
    int  param_count;
} FuncSig;

#define MAX_FUNC_SIGS 256
static FuncSig func_sigs[MAX_FUNC_SIGS];
static int func_sig_count = 0;

static void register_func_sig(const char* name, const char* ret,
                              ASTNode* decl) {
    if (func_sig_count >= MAX_FUNC_SIGS) return;
    FuncSig* sig = &func_sigs[func_sig_count++];
    strncpy(sig->name, name, 255); sig->name[255] = '\0';
    if (ret) { strncpy(sig->ret_type, ret, 31); sig->ret_type[31] = '\0'; }
    else sig->ret_type[0] = '\0';
    sig->param_count = decl->as.func_decl.param_count;
    for (int i = 0; i < sig->param_count && i < 16; i++) {
        if (decl->as.func_decl.params[i].type_name)
            strncpy(sig->param_types[i], decl->as.func_decl.params[i].type_name, 31);
        else
            sig->param_types[i][0] = '\0';
        sig->param_types[i][31] = '\0';
    }
}

static FuncSig* find_func_sig(const char* name) {
    for (int i = 0; i < func_sig_count; i++)
        if (strcmp(func_sigs[i].name, name) == 0) return &func_sigs[i];
    return nullptr;
}

/* Pre-scan AST to collect all function signatures */
static void prescan_signatures(ASTNode* program) {
    func_sig_count = 0;
    if (program->type != NODE_PROGRAM) return;
    for (int i = 0; i < program->as.program.count; i++) {
        ASTNode* n = program->as.program.nodes[i];
        if (n->type == NODE_FUNC_DECL) {
            /* RULE 1: Every function must declare a return type in static mode (except main) */
            if (compile_mode == MODE_STATIC && !n->as.func_decl.ret_type && strcmp(n->as.func_decl.name, "main") != 0) {
                char buf[512];
                snprintf(buf, sizeof(buf), "function '%s' in static mode must declare a return type.", n->as.func_decl.name);
                type_error(n->line, buf);
            }
            /* Check for duplicate function names */
            if (find_func_sig(n->as.func_decl.name)) {
                fprintf(stderr, "[Line %d] Error: Duplicate function '%s'.\n",
                        n->line, n->as.func_decl.name);
                had_type_error = true;
            }
            register_func_sig(n->as.func_decl.name, n->as.func_decl.ret_type, n);
        }
    }
}

/* ── Type inference (returns type name or nullptr for dynamic) ── */
static const char* infer_expr_type(ASTNode* node) {
    if (!node) return nullptr;
    switch (node->type) {
    case NODE_INT_LIT:    return "int";
    case NODE_FLOAT_LIT:  return "float";
    case NODE_STRING_LIT: return "string";
    case NODE_BOOL_LIT:   return "bool";
    case NODE_NULL_LIT:   return "null";
    case NODE_LIST_LIT:   return "list";
    case NODE_MAP_LIT:    return "map";
    case NODE_IDENTIFIER: {
        /* Look up variable type in locals */
        for (int i = current->local_count - 1; i >= 0; i--) {
            Local* l = &current->locals[i];
            if ((int)strlen(node->as.identifier.name) == l->length &&
                memcmp(node->as.identifier.name, l->name, l->length) == 0) {
                if (l->type_name[0]) return l->type_name;
                return nullptr;
            }
        }
        return nullptr; /* global or unknown */
    }
    case NODE_CALL: {
        if (node->as.call.callee->type == NODE_IDENTIFIER) {
            FuncSig* sig = find_func_sig(node->as.call.callee->as.identifier.name);
            if (sig && sig->ret_type[0]) return sig->ret_type;
        }
        return nullptr;
    }
    case NODE_BINARY: {
        const char* lt = infer_expr_type(node->as.binary.left);
        const char* rt = infer_expr_type(node->as.binary.right);
        TokenType op = node->as.binary.op;
        /* Comparison/logic always returns bool */
        if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL ||
            op == TOKEN_LESS || op == TOKEN_GREATER ||
            op == TOKEN_LESS_EQUAL || op == TOKEN_GREATER_EQUAL ||
            op == TOKEN_AND || op == TOKEN_OR)
            return "bool";
        /* String concat */
        if (op == TOKEN_PLUS && ((lt && strcmp(lt, "string") == 0) || (rt && strcmp(rt, "string") == 0)))
            return "string";
        /* Float promotion */
        if ((lt && strcmp(lt, "float") == 0) || (rt && strcmp(rt, "float") == 0))
            return "float";
        if (lt && strcmp(lt, "int") == 0 && rt && strcmp(rt, "int") == 0)
            return "int";
        return lt; /* best guess */
    }
    case NODE_UNARY:
        if (node->as.unary.op == TOKEN_BANG) return "bool";
        return infer_expr_type(node->as.unary.operand);
    default: return nullptr;
    }
}

static bool is_pointer_type(const char* type) {
    if (!type) return false;
    return strchr(type, '*') != nullptr;
}

static bool types_compatible(const char* expected, const char* actual) {
    if (!expected || !expected[0] || !actual) return true; /* dynamic = always OK */
    if (strcmp(expected, actual) == 0) return true;
    /* int and float are promotable */
    if (strcmp(expected, "float") == 0 && strcmp(actual, "int") == 0) return true;
    /* null is compatible with any pointer or dynamic */
    if (strcmp(actual, "null") == 0 && (is_pointer_type(expected) || !expected[0])) return true;
    return false;
}

static void type_error(int line, const char* msg) {
    fprintf(stderr, "[Line %d] Type Error: %s\n", line, msg);
    had_type_error = true;
}

/* Check if function is a builtin */
static bool is_builtin(const char* fn_name) {
    return strcmp(fn_name, "print") == 0 || strcmp(fn_name, "input") == 0 ||
           strcmp(fn_name, "len") == 0 || strcmp(fn_name, "range") == 0 ||
           strcmp(fn_name, "type") == 0 || strcmp(fn_name, "append") == 0 ||
           strcmp(fn_name, "getCurrentTime") == 0 || strcmp(fn_name, "toSeconds") == 0 ||
           strcmp(fn_name, "toMilliseconds") == 0 || strcmp(fn_name, "toMinutes") == 0 ||
           strcmp(fn_name, "toHours") == 0 ||
           strcmp(fn_name, "getProcessMemory") == 0 || strcmp(fn_name, "getVmMemory") == 0 ||
           strcmp(fn_name, "getVmPeakMemory") == 0 || strcmp(fn_name, "bytesToKB") == 0 ||
           strcmp(fn_name, "bytesToMB") == 0 || strcmp(fn_name, "bytesToGB") == 0;
}

/* Check function call argument types */
static void check_call_types(ASTNode* call_node) {
    if (call_node->as.call.callee->type != NODE_IDENTIFIER) return;
    const char* fn_name = call_node->as.call.callee->as.identifier.name;
    
    /* Skip builtin validation */
    if (is_builtin(fn_name)) return;

    FuncSig* sig = find_func_sig(fn_name);
    if (!sig) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Call to undefined function '%s'.", fn_name);
        type_error(call_node->line, buf);
        return;
    }
    
    if (compile_mode == MODE_DYNAMIC) return; /* skip arity/type in dynamic mode */
    
    if (call_node->as.call.arg_count != sig->param_count) return; /* arity checked at runtime */
    for (int i = 0; i < call_node->as.call.arg_count && i < sig->param_count; i++) {
        if (!sig->param_types[i][0]) continue; /* untyped param */
        const char* arg_type = infer_expr_type(call_node->as.call.args[i]);
        if (!arg_type) continue; /* can't infer = dynamic, allow */
        if (!types_compatible(sig->param_types[i], arg_type)) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "Function '%s' parameter %d expects '%s' but got '%s'.",
                     fn_name, i + 1, sig->param_types[i], arg_type);
            type_error(call_node->line, buf);
        }
    }
}

/* ── Helpers ──────────────────────────────────────── */
static Chunk* current_chunk() { return current->function->chunk; }

static void emit_byte(int line, uint8_t byte) { chunk_write(current_chunk(), byte, line); }
static void emit_bytes(int line, uint8_t a, uint8_t b) { emit_byte(line, a); emit_byte(line, b); }

static int emit_jump(int line, uint8_t op) {
    emit_byte(line, op);
    emit_byte(line, 0xff);
    emit_byte(line, 0xff);
    return current_chunk()->count - 2;
}

static void patch_jump(int offset) {
    int jump = current_chunk()->count - offset - 2;
    current_chunk()->code[offset]     = (jump >> 8) & 0xff;
    current_chunk()->code[offset + 1] = jump & 0xff;
}

static void emit_loop(int line, int loop_start) {
    emit_byte(line, OP_LOOP);
    int offset = current_chunk()->count - loop_start + 2;
    emit_byte(line, (offset >> 8) & 0xff);
    emit_byte(line, offset & 0xff);
}

static uint8_t make_constant(Value val) {
    int idx = chunk_add_constant(current_chunk(), val);
    return (uint8_t)idx;
}

static void emit_constant(int line, Value val) {
    emit_bytes(line, OP_CONSTANT, make_constant(val));
}

/* ── Scope management ─────────────────────────────── */
static void begin_scope()  { 
    current->scope_depth++;
    /* We don't have a reliable line number in begin_scope, use 0 */
    emit_byte(0, OP_ENTER_SCOPE);
}
static void end_scope(int line) {
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        Local* local = &current->locals[current->local_count - 1];

        bool is_param = (local->depth == 1 && current->function->name != nullptr);
        bool is_in_loop = (local->depth >= 2);
        bool is_hidden = (local->name[0] == '\0' || memcmp(local->name, "$", 1) == 0);

        if (local->holds_alloc && !is_hidden) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Memory leak detected. Pointer '%s' goes out of scope without being freed.", local->name);
            type_error(line, buf);
        }

        if (local->auto_free) {
            if (!suppress_autofree_notes) {
                printf("[Tantrums] note: auto-freed '%s' at line %d (provably local)\n", local->name, line);
            }
            emit_bytes(line, OP_GET_LOCAL, (uint8_t)(current->local_count - 1));
            emit_byte(line, OP_FREE);
        }

        if (!local->is_used && current->function->name != nullptr && !is_hidden && !is_param && !is_in_loop) {
            fprintf(stderr, "[Line %d] Warning: Unused variable '%s'.\n", line, local->name);
        }
        emit_byte(line, OP_POP);
        current->local_count--;
    }
    /* OP_EXIT_SCOPE goes AFTER all auto-frees and pops, so Layer 2 only
       sees pointers that were NOT already freed by compile-time bytecode. */
    emit_byte(line, OP_EXIT_SCOPE);
}

static int add_local(const char* name, int len, const char* type = nullptr) {
    if (current->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Too many local variables.\n");
        return -1;
    }
    Local* local = &current->locals[current->local_count];
    int copy_len = len < 255 ? len : 255;
    memcpy(local->name, name, copy_len);
    local->name[copy_len] = '\0';
    local->length = copy_len;
    local->depth = current->scope_depth;
    local->is_used = false;
    local->holds_alloc = false;
    local->auto_free = false;
    if (type) { strncpy(local->type_name, type, 31); local->type_name[31] = '\0'; }
    else local->type_name[0] = '\0';
    return current->local_count++;
}

static int resolve_local(CompilerState* state, const char* name, int len) {
    for (int i = state->local_count - 1; i >= 0; i--) {
        Local* l = &state->locals[i];
        if (l->length == len && memcmp(l->name, name, len) == 0) {
            l->is_used = true;
            return i;
        }
    }
    return -1;
}

/* ── Compile AST nodes ────────────────────────────── */
static void compile_node(ASTNode* node);

static void compile_expr(ASTNode* node) {
    if (!node) { emit_byte(0, OP_NULL); return; }
    switch (node->type) {
    case NODE_INT_LIT:
        emit_constant(node->line, INT_VAL(node->as.int_literal)); break;
    case NODE_FLOAT_LIT:
        emit_constant(node->line, FLOAT_VAL(node->as.float_literal)); break;
    case NODE_STRING_LIT: {
        ObjString* s = obj_string_new(node->as.string_literal.value, node->as.string_literal.length);
        emit_constant(node->line, OBJ_VAL(s));
    } break;
    case NODE_BOOL_LIT:
        emit_byte(node->line, node->as.bool_literal ? OP_TRUE : OP_FALSE); break;
    case NODE_NULL_LIT:
        emit_byte(node->line, OP_NULL); break;

    case NODE_IDENTIFIER: {
        int slot = resolve_local(current, node->as.identifier.name, node->as.identifier.length);
        if (slot != -1) {
            emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)slot);
        } else {
            ObjString* name = obj_string_new(node->as.identifier.name, node->as.identifier.length);
            emit_bytes(node->line, OP_GET_GLOBAL, make_constant(OBJ_VAL(name)));
        }
    } break;

    case NODE_UNARY:
        compile_expr(node->as.unary.operand);
        switch (node->as.unary.op) {
        case TOKEN_MINUS:     emit_byte(node->line, OP_NEGATE); break;
        case TOKEN_BANG:      emit_byte(node->line, OP_NOT); break;
        case TOKEN_AMPERSAND: emit_byte(node->line, OP_PTR_REF); break;
        case TOKEN_STAR:      emit_byte(node->line, OP_PTR_DEREF); break;
        default: break;
        }
        break;

    case NODE_BINARY:
        /* Short-circuit AND */
        if (node->as.binary.op == TOKEN_AND) {
            compile_expr(node->as.binary.left);
            int end_jump = emit_jump(node->line, OP_JUMP_IF_FALSE);
            emit_byte(node->line, OP_POP);
            compile_expr(node->as.binary.right);
            patch_jump(end_jump);
            break;
        }
        /* Short-circuit OR */
        if (node->as.binary.op == TOKEN_OR) {
            compile_expr(node->as.binary.left);
            int else_jump = emit_jump(node->line, OP_JUMP_IF_FALSE);
            int end_jump = emit_jump(node->line, OP_JUMP);
            patch_jump(else_jump);
            emit_byte(node->line, OP_POP);
            compile_expr(node->as.binary.right);
            patch_jump(end_jump);
            break;
        }
        compile_expr(node->as.binary.left);
        compile_expr(node->as.binary.right);
        switch (node->as.binary.op) {
        case TOKEN_PLUS:          emit_byte(node->line, OP_ADD); break;
        case TOKEN_MINUS:         emit_byte(node->line, OP_SUB); break;
        case TOKEN_STAR:          emit_byte(node->line, OP_MUL); break;
        case TOKEN_SLASH:
            /* Compile-time division by zero check */
            if (node->as.binary.right->type == NODE_INT_LIT &&
                node->as.binary.right->as.int_literal == 0) {
                type_error(node->line, "Division by zero.");
            }
            if (node->as.binary.right->type == NODE_FLOAT_LIT &&
                node->as.binary.right->as.float_literal == 0.0) {
                type_error(node->line, "Division by zero.");
            }
            emit_byte(node->line, OP_DIV); break;
        case TOKEN_PERCENT:       emit_byte(node->line, OP_MOD); break;
        case TOKEN_EQUAL_EQUAL:   emit_byte(node->line, OP_EQ); break;
        case TOKEN_BANG_EQUAL:    emit_byte(node->line, OP_NEQ); break;
        case TOKEN_LESS:          emit_byte(node->line, OP_LT); break;
        case TOKEN_GREATER:       emit_byte(node->line, OP_GT); break;
        case TOKEN_LESS_EQUAL:    emit_byte(node->line, OP_LTE); break;
        case TOKEN_GREATER_EQUAL: emit_byte(node->line, OP_GTE); break;
        default: break;
        }
        break;

    case NODE_CALL: {
        check_call_types(node); /* compile-time type check */
        
        /* RULE 6: void functions cannot be used in expressions in static mode */
        const char* rt = infer_expr_type(node);
        if (compile_mode == MODE_STATIC && rt && strcmp(rt, "void") == 0 && !is_in_expr_stmt) {
            char buf[512];
            if (node->as.call.callee->type == NODE_IDENTIFIER) {
                snprintf(buf, sizeof(buf), "'%s' is void and cannot be used in an expression.", 
                         node->as.call.callee->as.identifier.name);
            } else {
                snprintf(buf, sizeof(buf), "void function call cannot be used in an expression.");
            }
            type_error(node->line, buf);
        }

        bool old_expr_stmt = is_in_expr_stmt;
        is_in_expr_stmt = false; // Arguments/callee are not in expr stmt context
        compile_expr(node->as.call.callee);
        
        bool is_user_func = false;
        if (node->as.call.callee->type == NODE_IDENTIFIER && !is_builtin(node->as.call.callee->as.identifier.name)) {
            is_user_func = true;
        }

        for (int i = 0; i < node->as.call.arg_count; i++) {
            if (is_user_func && node->as.call.args[i]->type == NODE_IDENTIFIER) {
                int slot = resolve_local(current, node->as.call.args[i]->as.identifier.name, node->as.call.args[i]->as.identifier.length);
                if (slot != -1) current->locals[slot].holds_alloc = false;
            }
            compile_expr(node->as.call.args[i]);
        }
        is_in_expr_stmt = old_expr_stmt;

        emit_bytes(node->line, OP_CALL, (uint8_t)node->as.call.arg_count);
    } break;

    case NODE_LIST_LIT: {
        for (int i = 0; i < node->as.list_literal.count; i++)
            compile_expr(node->as.list_literal.nodes[i]);
        emit_bytes(node->line, OP_LIST_NEW, (uint8_t)node->as.list_literal.count);
    } break;

    case NODE_MAP_LIT: {
        for (int i = 0; i < node->as.map_literal.count; i++) {
            compile_expr(node->as.map_literal.keys[i]);
            compile_expr(node->as.map_literal.values[i]);
        }
        emit_bytes(node->line, OP_MAP_NEW, (uint8_t)node->as.map_literal.count);
    } break;

    case NODE_INDEX:
        compile_expr(node->as.index_access.object);
        compile_expr(node->as.index_access.index);
        emit_byte(node->line, OP_INDEX_GET);
        break;

    case NODE_ALLOC: {
        compile_expr(node->as.alloc_expr.init);
        emit_byte(node->line, OP_ALLOC);
        const char* type_name = node->as.alloc_expr.type_name ? node->as.alloc_expr.type_name : "dynamic";
        ObjString* type_str = obj_string_new(type_name, (int)strlen(type_name));
        emit_byte(node->line, make_constant(OBJ_VAL(type_str)));
        break;
    }

    case NODE_POSTFIX: {
        bool is_inc = node->as.postfix.op == TOKEN_PLUS_PLUS;
        if (node->as.postfix.operand->type == NODE_IDENTIFIER) {
            int slot = resolve_local(current, node->as.postfix.operand->as.identifier.name, node->as.postfix.operand->as.identifier.length);
            
            /* 1. Put old value on stack */
            compile_expr(node->as.postfix.operand);
            
            /* 2. Compute new value */
            compile_expr(node->as.postfix.operand);
            emit_constant(node->line, INT_VAL(1));
            emit_byte(node->line, is_inc ? OP_ADD : OP_SUB);
            
            /* 3. Set (leaves new value on stack) */
            if (slot != -1) {
                emit_bytes(node->line, OP_SET_LOCAL, (uint8_t)slot);
            } else {
                ObjString* name = obj_string_new(node->as.postfix.operand->as.identifier.name, node->as.postfix.operand->as.identifier.length);
                emit_bytes(node->line, OP_SET_GLOBAL, make_constant(OBJ_VAL(name)));
            }
            /* 4. Pop new value, leaving the OLD value generated in step 1 on top of the stack! */
            emit_byte(node->line, OP_POP);
        } else {
            type_error(node->line, "Invalid operand for postfix operation.");
            emit_byte(node->line, OP_NULL);
        }
    } break;

    case NODE_ASSIGN: {
        /* STATIC mode: error if assigning to undeclared (untyped) variable */
        if (compile_mode == MODE_STATIC) {
            int s = resolve_local(current, node->as.assign.name, (int)strlen(node->as.assign.name));
            if (s == -1) {
                char buf[512];
                snprintf(buf, sizeof(buf),
                         "Static mode: variable '%s' must be declared with a type (e.g., int %s = ...).",
                         node->as.assign.name, node->as.assign.name);
                type_error(node->line, buf);
            }
        }

        /* Type check assignment to typed local (skip in DYNAMIC mode) */
        if (compile_mode != MODE_DYNAMIC) {
            int check_slot = resolve_local(current, node->as.assign.name, (int)strlen(node->as.assign.name));
            if (check_slot != -1 && current->locals[check_slot].type_name[0]) {
                const char* val_type = infer_expr_type(node->as.assign.value);
                if (val_type && !types_compatible(current->locals[check_slot].type_name, val_type)) {
                    char buf[512];
                    snprintf(buf, sizeof(buf),
                             "Cannot assign '%s' value to '%s' variable '%s'.",
                             val_type, current->locals[check_slot].type_name, node->as.assign.name);
                    type_error(node->line, buf);
                }
            }
        }
        compile_expr(node->as.assign.value);
        int slot = resolve_local(current, node->as.assign.name, (int)strlen(node->as.assign.name));
        
        if (slot != -1) {
            if (current->locals[slot].holds_alloc) {
                char buf[512];
                snprintf(buf, sizeof(buf), "Memory leak detected. Pointer '%s' reassigned without being freed.", node->as.assign.name);
                type_error(node->line, buf);
            }
            if (node->as.assign.value && node->as.assign.value->type == NODE_ALLOC) {
                current->locals[slot].holds_alloc = true;
            } else if (node->as.assign.value && node->as.assign.value->type == NODE_CALL && node->as.assign.value->as.call.callee->type == NODE_IDENTIFIER) {
                FuncSig* sig = find_func_sig(node->as.assign.value->as.call.callee->as.identifier.name);
                if (sig && is_pointer_type(sig->ret_type)) {
                    current->locals[slot].holds_alloc = true;
                } else {
                    current->locals[slot].holds_alloc = false;
                }
            } else {
                current->locals[slot].holds_alloc = false;
            }
        }
        
        if (slot != -1) {
            /* Reassign existing local — set, leaves copy on stack */
            emit_bytes(node->line, OP_SET_LOCAL, (uint8_t)slot);
        } else if (current->scope_depth > 0) {
            /* Inside a function: create a new local variable. */
            int new_slot = add_local(node->as.assign.name, (int)strlen(node->as.assign.name));
            emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)new_slot);
        } else {
            /* Top-level: create/update a global */
            ObjString* name = obj_string_new(node->as.assign.name, (int)strlen(node->as.assign.name));
            emit_bytes(node->line, OP_SET_GLOBAL, make_constant(OBJ_VAL(name)));
        }
    } break;

    case NODE_INDEX_ASSIGN: {
        if (node->as.index_assign.index == nullptr) {
            /* Pointer dereference assignment: *ptr = val */
            compile_expr(node->as.index_assign.value);
            compile_expr(node->as.index_assign.object);
            emit_byte(node->line, OP_PTR_SET);
        } else {
            compile_expr(node->as.index_assign.object);
            compile_expr(node->as.index_assign.index);
            compile_expr(node->as.index_assign.value);
            emit_byte(node->line, OP_INDEX_SET);
        }
    } break;

    default:
        fprintf(stderr, "[Line %d] Cannot compile expression of type %d\n", node->line, node->type);
        break;
    }
}

/* ── Control Flow Analysis ────────────────────────── */
static bool has_guaranteed_return(ASTNode* node) {
    if (!node) return false;
    switch (node->type) {
    case NODE_RETURN:
    case NODE_THROW:
        return true;
    case NODE_BLOCK:
        for (int i = 0; i < node->as.block.count; i++) {
            if (has_guaranteed_return(node->as.block.nodes[i])) return true;
        }
        return false;
    case NODE_IF:
        /* Both branches must exist and guarantee return to assert the whole IF guarantees return */
        if (node->as.if_stmt.else_b) {
            return has_guaranteed_return(node->as.if_stmt.then_b) &&
                   has_guaranteed_return(node->as.if_stmt.else_b);
        }
        return false;
    case NODE_TRY_CATCH:
        return has_guaranteed_return(node->as.try_catch.try_body) &&
               has_guaranteed_return(node->as.try_catch.catch_body);
    default:
        return false;
    }
}

static bool has_any_return(ASTNode* node) {
    if (!node) return false;
    switch (node->type) {
    case NODE_RETURN:
        return true;
    case NODE_BLOCK:
        for (int i = 0; i < node->as.block.count; i++) {
            if (has_any_return(node->as.block.nodes[i])) return true;
        }
        return false;
    case NODE_IF:
        return has_any_return(node->as.if_stmt.then_b) ||
               (node->as.if_stmt.else_b && has_any_return(node->as.if_stmt.else_b));
    case NODE_TRY_CATCH:
        return has_any_return(node->as.try_catch.try_body) ||
               has_any_return(node->as.try_catch.catch_body);
    default:
        return false;
    }
}

/* ── Escape Analysis (Layer 1) ────────────────────── */
typedef struct {
    bool escaped;
    bool has_manual_free;
    int  use_count;
    bool only_simple_assign;
} EscapeResult;

static void analyze_escape(ASTNode* node, const char* target_name, int loop_depth, EscapeResult* result) {
    if (!node || result->escaped) return;

    switch (node->type) {
    case NODE_IDENTIFIER: {
        if (strcmp(node->as.identifier.name, target_name) == 0) {
            result->use_count++;
            /* Condition 6: Conditional escape */
            if (loop_depth > 0) result->escaped = true;
            /* Condition 7: Expression escape (if we reach here, it's not a direct LHS of standalone assign) */
            else result->escaped = true;
        }
        break;
    }
    case NODE_RETURN: {
        /* Condition 1: Return escape */
        EscapeResult ret_check = {false, false, 0, true};
        analyze_escape(node->as.child, target_name, loop_depth, &ret_check);
        if (ret_check.use_count > 0) result->escaped = true;
        break;
    }
    case NODE_EXPR_STMT: {
        /* Check if this is a lone deref assignment `*p = value;` */
        if (node->as.child && node->as.child->type == NODE_INDEX_ASSIGN) {
            ASTNode* assign = node->as.child;
            if (assign->as.index_assign.index == nullptr && /* pointer deref */
                assign->as.index_assign.object->type == NODE_IDENTIFIER &&
                strcmp(assign->as.index_assign.object->as.identifier.name, target_name) == 0) {
                
                result->use_count++;
                if (loop_depth > 0) result->escaped = true;
                
                /* Analyze the RHS value normally */
                analyze_escape(assign->as.index_assign.value, target_name, loop_depth, result);
                return; /* Don't traverse object (would trigger condition 7) */
            }
        }
        analyze_escape(node->as.child, target_name, loop_depth, result);
        break;
    }
    case NODE_CALL: {
        /* Condition 2: Function call escape */
        for (int i = 0; i < node->as.call.arg_count; i++) {
            EscapeResult arg_check = {false, false, 0, true};
            analyze_escape(node->as.call.args[i], target_name, loop_depth, &arg_check);
            if (arg_check.use_count > 0) result->escaped = true;
        }
        analyze_escape(node->as.call.callee, target_name, loop_depth, result);
        break;
    }
    case NODE_ASSIGN:
    case NODE_VAR_DECL: {
        /* Condition 3/5: Alias / Global escape */
        ASTNode* rhs = (node->type == NODE_ASSIGN) ? node->as.assign.value : node->as.var_decl.init;
        EscapeResult rhs_check = {false, false, 0, true};
        analyze_escape(rhs, target_name, loop_depth, &rhs_check);
        if (rhs_check.use_count > 0) result->escaped = true;
        break;
    }
    case NODE_INDEX_ASSIGN: {
        /* Condition 4: Map/List escape */
        EscapeResult val_check = {false, false, 0, true};
        analyze_escape(node->as.index_assign.value, target_name, loop_depth, &val_check);
        if (val_check.use_count > 0) result->escaped = true;
        
        analyze_escape(node->as.index_assign.object, target_name, loop_depth, result);
        if (node->as.index_assign.index) analyze_escape(node->as.index_assign.index, target_name, loop_depth, result);
        break;
    }
    case NODE_FREE: {
        if (node->as.child && node->as.child->type == NODE_IDENTIFIER) {
            if (strcmp(node->as.child->as.identifier.name, target_name) == 0) {
                if (loop_depth > 0) result->escaped = true; /* Ambigous manual free */
                else {
                    result->use_count++;
                    result->has_manual_free = true;
                }
                return;
            }
        }
        analyze_escape(node->as.child, target_name, loop_depth, result);
        break;
    }
    case NODE_BLOCK: {
        for (int i = 0; i < node->as.block.count; i++) {
            analyze_escape(node->as.block.nodes[i], target_name, loop_depth, result);
        }
        break;
    }
    case NODE_IF: {
        analyze_escape(node->as.if_stmt.cond, target_name, loop_depth, result);
        analyze_escape(node->as.if_stmt.then_b, target_name, loop_depth + 1, result);
        if (node->as.if_stmt.else_b) analyze_escape(node->as.if_stmt.else_b, target_name, loop_depth + 1, result);
        break;
    }
    case NODE_WHILE: {
        analyze_escape(node->as.while_stmt.cond, target_name, loop_depth, result);
        analyze_escape(node->as.while_stmt.body, target_name, loop_depth + 1, result);
        break;
    }
    case NODE_FOR_IN: {
        analyze_escape(node->as.for_in.iterable, target_name, loop_depth, result);
        analyze_escape(node->as.for_in.body, target_name, loop_depth + 1, result);
        break;
    }
    case NODE_BINARY:
        analyze_escape(node->as.binary.left, target_name, loop_depth, result);
        analyze_escape(node->as.binary.right, target_name, loop_depth, result);
        break;
    case NODE_UNARY:
        /* TOKEN_STAR is a pointer dereference (*p) — reading through p, not escaping it.
           Do NOT recurse into the operand in this case; p stays local. */
        if (node->as.unary.op != TOKEN_STAR) {
            analyze_escape(node->as.unary.operand, target_name, loop_depth, result);
        }
        break;
    case NODE_INDEX:
        analyze_escape(node->as.index_access.object, target_name, loop_depth, result);
        analyze_escape(node->as.index_access.index, target_name, loop_depth, result);
        break;
    case NODE_POSTFIX:
        analyze_escape(node->as.postfix.operand, target_name, loop_depth, result);
        break;
    case NODE_ALLOC:
        analyze_escape(node->as.alloc_expr.init, target_name, loop_depth, result);
        break;
    case NODE_LIST_LIT:
        for (int i = 0; i < node->as.list_literal.count; i++) analyze_escape(node->as.list_literal.nodes[i], target_name, loop_depth, result);
        break;
    case NODE_MAP_LIT:
        for (int i = 0; i < node->as.map_literal.count; i++) {
            analyze_escape(node->as.map_literal.keys[i], target_name, loop_depth, result);
            analyze_escape(node->as.map_literal.values[i], target_name, loop_depth, result);
        }
        break;
    case NODE_TRY_CATCH:
        analyze_escape(node->as.try_catch.try_body, target_name, loop_depth + 1, result);
        analyze_escape(node->as.try_catch.catch_body, target_name, loop_depth + 1, result);
        break;
    default:
        break;
    }
    
    /* Condition 8: Multiple usages (excluding direct reassignments of the pointer itself which is caught layer) */
    if (result->use_count > (result->has_manual_free ? 2 : 1)) {
        result->escaped = true;
    }
}

static void compile_node(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
    case NODE_EXPR_STMT: {
        /* RULE 6: Warn if a pointer return value is discarded */
        if (node->as.child && node->as.child->type == NODE_CALL && node->as.child->as.call.callee->type == NODE_IDENTIFIER) {
            FuncSig* sig = find_func_sig(node->as.child->as.call.callee->as.identifier.name);
            if (sig && is_pointer_type(sig->ret_type)) {
                fprintf(stderr, "[Tantrums Warning] line %d: pointer return value discarded — potential leak.\n", node->line);
            }
        }
        
        bool old = is_in_expr_stmt;
        is_in_expr_stmt = true;
        compile_expr(node->as.child);
        is_in_expr_stmt = old;
        emit_byte(node->line, OP_POP);
        break;
    }

    case NODE_VAR_DECL: {
        /* Check for shadowing builtins */
        if (strcmp(node->as.var_decl.name, "print") == 0 || strcmp(node->as.var_decl.name, "input") == 0 ||
            strcmp(node->as.var_decl.name, "len") == 0 || strcmp(node->as.var_decl.name, "range") == 0 ||
            strcmp(node->as.var_decl.name, "type") == 0 || strcmp(node->as.var_decl.name, "append") == 0 ||
            strcmp(node->as.var_decl.name, "getCurrentTime") == 0 || strcmp(node->as.var_decl.name, "toSeconds") == 0 ||
            strcmp(node->as.var_decl.name, "toMilliseconds") == 0 || strcmp(node->as.var_decl.name, "toMinutes") == 0 ||
            strcmp(node->as.var_decl.name, "toHours") == 0) {
            fprintf(stderr, "[Line %d] Warning: Variable '%s' shadows a built-in function.\n", node->line, node->as.var_decl.name);
        }

        /* Check for duplicate declarations (same scope) */
        if (current->scope_depth == 0) {
            if (is_global_tracked(node->as.var_decl.name)) {
                char buf[512];
                snprintf(buf, sizeof(buf), "Duplicate global variable declaration '%s'.", node->as.var_decl.name);
                type_error(node->line, buf);
            }
        } else {
            for (int i = current->local_count - 1; i >= 0; i--) {
                Local* l = &current->locals[i];
                if (l->depth < current->scope_depth) break; // Out of current scope layer (only checked exact same scope)
                if (l->length == (int)strlen(node->as.var_decl.name) &&
                    memcmp(l->name, node->as.var_decl.name, l->length) == 0) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "Duplicate variable declaration '%s' in the same scope.", node->as.var_decl.name);
                    type_error(node->line, buf);
                }
            }
        }

        /* Type check: if typed and init has inferable type, verify (skip in DYNAMIC mode) */
        if (compile_mode != MODE_DYNAMIC && node->as.var_decl.type_name && node->as.var_decl.init) {
            bool is_ptr = node->as.var_decl.init->type == NODE_ALLOC;
            if (!is_ptr) {
                const char* init_type = infer_expr_type(node->as.var_decl.init);
                if (init_type && !types_compatible(node->as.var_decl.type_name, init_type)) {
                    char buf[512];
                    snprintf(buf, sizeof(buf),
                             "Cannot assign '%s' value to '%s' variable '%s'.",
                             init_type, node->as.var_decl.type_name, node->as.var_decl.name);
                    type_error(node->line, buf);
                }
            }
        }

        if (node->as.var_decl.init) {
            compile_expr(node->as.var_decl.init);
        } else {
            /* Default Initialization based on type */
            if (node->as.var_decl.type_name) {
                if (strcmp(node->as.var_decl.type_name, "list") == 0) {
                    emit_bytes(node->line, OP_LIST_NEW, 0);
                } else if (strcmp(node->as.var_decl.type_name, "map") == 0) {
                    emit_bytes(node->line, OP_MAP_NEW, 0);
                } else if (strcmp(node->as.var_decl.type_name, "int") == 0) {
                    emit_constant(node->line, INT_VAL(0));
                } else if (strcmp(node->as.var_decl.type_name, "float") == 0) {
                    emit_constant(node->line, FLOAT_VAL(0.0));
                } else if (strcmp(node->as.var_decl.type_name, "bool") == 0) {
                    emit_byte(node->line, OP_FALSE);
                } else if (strcmp(node->as.var_decl.type_name, "string") == 0) {
                    ObjString* empty_str = obj_string_new("", 0);
                    emit_constant(node->line, OBJ_VAL(empty_str));
                } else {
                    emit_byte(node->line, OP_NULL);
                }
            } else {
                emit_byte(node->line, OP_NULL);
            }
        }

        /* Auto-cast if type annotation present */
        if (node->as.var_decl.type_name && (!node->as.var_decl.init || node->as.var_decl.init->type != NODE_ALLOC)) {
            uint8_t cast_tag = 255;
            if (strcmp(node->as.var_decl.type_name, "int") == 0)    cast_tag = 0;
            if (strcmp(node->as.var_decl.type_name, "float") == 0)  cast_tag = 1;
            if (strcmp(node->as.var_decl.type_name, "string") == 0) cast_tag = 2;
            if (strcmp(node->as.var_decl.type_name, "bool") == 0)   cast_tag = 3;
            if (cast_tag != 255)
                emit_bytes(node->line, OP_CAST, cast_tag);
        }

        if (current->scope_depth > 0) {
            int slot = add_local(node->as.var_decl.name, (int)strlen(node->as.var_decl.name),
                                 node->as.var_decl.type_name);
            if (slot != -1 && node->as.var_decl.init) {
                if (node->as.var_decl.init->type == NODE_ALLOC) {
                    current->locals[slot].holds_alloc = true;
                } else if (node->as.var_decl.init->type == NODE_CALL && node->as.var_decl.init->as.call.callee->type == NODE_IDENTIFIER) {
                    FuncSig* sig = find_func_sig(node->as.var_decl.init->as.call.callee->as.identifier.name);
                    if (sig && is_pointer_type(sig->ret_type)) {
                        current->locals[slot].holds_alloc = true;
                    }
                }
            }
        } else {
            track_global(node->as.var_decl.name);
            ObjString* name = obj_string_new(node->as.var_decl.name, (int)strlen(node->as.var_decl.name));
            emit_bytes(node->line, OP_DEFINE_GLOBAL, make_constant(OBJ_VAL(name)));
        }
    } break;


    case NODE_BLOCK:
        if (node->as.block.count == 0 && compile_mode != MODE_DYNAMIC) { /* Optional empty block  */
            /* Ignore empty body warning by default to reduce noise */
        }
        begin_scope();
        for (int i = 0; i < node->as.block.count; i++) {

            compile_node(node->as.block.nodes[i]);

            /* If this was a var_decl that initialized a pointer, NOW the slot exists —
               run escape analysis on the remaining nodes and update holds_alloc / auto_free */
            if (node->as.block.nodes[i]->type == NODE_VAR_DECL &&
                node->as.block.nodes[i]->as.var_decl.init &&
                node->as.block.nodes[i]->as.var_decl.init->type == NODE_ALLOC) {

                const char* target_name = node->as.block.nodes[i]->as.var_decl.name;
                int slot = resolve_local(current, target_name, (int)strlen(target_name));
                if (slot != -1) {
                    EscapeResult er = {false, false, 0, true};
                    for (int j = i + 1; j < node->as.block.count; j++) {
                        analyze_escape(node->as.block.nodes[j], target_name, 0, &er);
                        if (er.escaped) break;
                    }

                    if (er.escaped) {
                        current->locals[slot].holds_alloc = false; /* Priority 1/4 — escaped, trust developer */
                    } else if (er.has_manual_free) {
                        current->locals[slot].holds_alloc = false; /* Priority 2 — developer freed it manually */
                    } else if (er.use_count == 1) {
                        current->locals[slot].auto_free = true;    /* Priority 3 — provably local, insert auto-free */
                        current->locals[slot].holds_alloc = false;
                    } else {
                        current->locals[slot].holds_alloc = false; /* Ambiguous — do not error, let Layer 2 handle it */
                    }
                }
            }

            if (node->as.block.nodes[i]->type == NODE_RETURN || node->as.block.nodes[i]->type == NODE_THROW) {
                if (i < node->as.block.count - 1) {
                    fprintf(stderr, "[Line %d] Warning: Unreachable code after return/throw.\n", node->as.block.nodes[i+1]->line);
                }
                break;
            }
        }
        end_scope(node->line);
        break;

    case NODE_IF: {
        compile_expr(node->as.if_stmt.cond);
        int then_jump = emit_jump(node->line, OP_JUMP_IF_FALSE);
        emit_byte(node->line, OP_POP); /* pop condition */
        /* Compile then-body statements directly — do NOT emit OP_ENTER_SCOPE/OP_EXIT_SCOPE
           for if/else bodies. Jumps can skip OP_ENTER_SCOPE but not its matching
           OP_EXIT_SCOPE (or vice versa), which corrupts vm->scope_depth permanently. */
        if (node->as.if_stmt.then_b && node->as.if_stmt.then_b->type == NODE_BLOCK) {
            for (int i = 0; i < node->as.if_stmt.then_b->as.block.count; i++)
                compile_node(node->as.if_stmt.then_b->as.block.nodes[i]);
        } else {
            compile_node(node->as.if_stmt.then_b);
        }
        int else_jump = emit_jump(node->line, OP_JUMP);
        patch_jump(then_jump);
        emit_byte(node->line, OP_POP);
        if (node->as.if_stmt.else_b) {
            if (node->as.if_stmt.else_b->type == NODE_BLOCK) {
                for (int i = 0; i < node->as.if_stmt.else_b->as.block.count; i++)
                    compile_node(node->as.if_stmt.else_b->as.block.nodes[i]);
            } else {
                compile_node(node->as.if_stmt.else_b);
            }
        }
        patch_jump(else_jump);
    } break;

    case NODE_WHILE: {
        Loop loop;
        loop.enclosing = current_loop;
        loop.start = current_chunk()->count;
        loop.scope_depth = current->scope_depth;
        loop.type = 0;
        loop.break_count = 0;
        loop.continue_count = 0;
        current_loop = &loop;

        compile_expr(node->as.while_stmt.cond);
        int exit_jump = emit_jump(node->line, OP_JUMP_IF_FALSE);
        emit_byte(node->line, OP_POP);
        compile_node(node->as.while_stmt.body);
        emit_loop(node->line, loop.start);
        patch_jump(exit_jump);
        emit_byte(node->line, OP_POP);

        for (int i = 0; i < loop.break_count; i++) {
            patch_jump(loop.breaks[i]);
        }
        current_loop = loop.enclosing;
    } break;

    case NODE_FOR_IN: {
        /* Compile iterable onto stack, clone it, then hidden counter */
        begin_scope();
        compile_expr(node->as.for_in.iterable);
        emit_byte(node->line, OP_CLONE);
        int iterable_slot = add_local("$iter", 5);
        emit_constant(node->line, INT_VAL(0));
        int counter_slot = add_local("$idx", 4);
        /* Loop variable */
        emit_byte(node->line, OP_NULL);
        int var_slot = add_local(node->as.for_in.var_name, (int)strlen(node->as.for_in.var_name));

        int loop_start = current_chunk()->count;
        /* Check: counter < len(iterable) */
        emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)counter_slot);
        emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)iterable_slot);
        emit_byte(node->line, OP_LEN);
        emit_byte(node->line, OP_LT);
        int exit_jump = emit_jump(node->line, OP_JUMP_IF_FALSE);
        emit_byte(node->line, OP_POP);

        /* var = iterable[counter] */
        emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)iterable_slot);
        emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)counter_slot);
        emit_byte(node->line, OP_INDEX_GET);
        emit_bytes(node->line, OP_SET_LOCAL, (uint8_t)var_slot);
        emit_byte(node->line, OP_POP);

        Loop loop;
        loop.enclosing = current_loop;
        loop.start = loop_start;
        loop.scope_depth = current->scope_depth;
        loop.type = 1;
        loop.break_count = 0;
        loop.continue_count = 0;
        current_loop = &loop;

        /* Body */
        compile_node(node->as.for_in.body);

        /* counter++ */
        for (int i = 0; i < loop.continue_count; i++) {
            patch_jump(loop.continues[i]);
        }

        emit_bytes(node->line, OP_GET_LOCAL, (uint8_t)counter_slot);
        emit_constant(node->line, INT_VAL(1));
        emit_byte(node->line, OP_ADD);
        emit_bytes(node->line, OP_SET_LOCAL, (uint8_t)counter_slot);
        emit_byte(node->line, OP_POP);

        emit_loop(node->line, loop_start);
        patch_jump(exit_jump);
        emit_byte(node->line, OP_POP);

        for (int i = 0; i < loop.break_count; i++) {
            patch_jump(loop.breaks[i]);
        }

        current_loop = loop.enclosing;
        end_scope(node->line);
    } break;

    case NODE_FUNC_DECL: {
        ObjFunction* fn = obj_function_new();
        fn->name = obj_string_new(node->as.func_decl.name, (int)strlen(node->as.func_decl.name));
        fn->arity = node->as.func_decl.param_count;

        CompilerState comp;
        memset(&comp, 0, sizeof(comp));
        comp.function = fn;
        comp.enclosing = current;
        comp.scope_depth = 1;
        if (node->as.func_decl.ret_type) {
            strncpy(comp.ret_type, node->as.func_decl.ret_type, 31);
            comp.ret_type[31] = '\0';
        } else {
            comp.ret_type[0] = '\0';
        }
        current = &comp;

        /* Reserve slot 0 for the function itself */
        add_local("", 0);

        /* Parameters as locals */
        for (int i = 0; i < node->as.func_decl.param_count; i++) {
            add_local(node->as.func_decl.params[i].name,
                      (int)strlen(node->as.func_decl.params[i].name),
                      node->as.func_decl.params[i].type_name);
        }

        /* Compile body (it's a block, but don't add extra scope) */
        ASTNode* body = node->as.func_decl.body;
        bool has_return = has_guaranteed_return(body);
        compile_node(body);

        /* RULE 3 & 4: non-void functions must return the declared type on all paths in static mode */
        if (compile_mode == MODE_STATIC && comp.ret_type[0] && strcmp(comp.ret_type, "void") != 0) {
            if (!has_return) {
                char buf[512];
                if (strstr(body->as.block.nodes[body->as.block.count-1]->type == NODE_IF ? "" : "", "")) {} // dummy
                /* We need to distinguish between "no return at all" and "not all paths" */
                if (!has_any_return(body)) {
                    snprintf(buf, sizeof(buf), "function '%s' declared '%s' but has no return statement.", 
                             node->as.func_decl.name, comp.ret_type);
                } else {
                    snprintf(buf, sizeof(buf), "function '%s' not all code paths return a value.", 
                             node->as.func_decl.name);
                }
                type_error(node->line, buf);
            }
        } else if (!has_return && node->as.func_decl.ret_type && 
            strcmp(node->as.func_decl.ret_type, "null") != 0 &&
            strcmp(node->as.func_decl.ret_type, "void") != 0) {
            fprintf(stderr, "[Line %d] Warning: Function '%s' is typed as '%s' but may lack a return statement.\n", 
                    node->line, node->as.func_decl.name, node->as.func_decl.ret_type);
        }

        /* Implicit return null */
        emit_byte(node->line, OP_NULL);
        emit_byte(node->line, OP_RETURN);

        current = comp.enclosing;

        /* Define function as global */
        emit_constant(node->line, OBJ_VAL(fn));
        if (current->scope_depth > 0) {
            add_local(node->as.func_decl.name, (int)strlen(node->as.func_decl.name));
        } else {
            ObjString* name = obj_string_new(node->as.func_decl.name, (int)strlen(node->as.func_decl.name));
            emit_bytes(node->line, OP_DEFINE_GLOBAL, make_constant(OBJ_VAL(name)));
        }
    } break;

    case NODE_RETURN: {
        if (current->function->name == nullptr) {
            type_error(node->line, "'return' statement used outside of a function.");
        }

        /* Escape Analysis: identify if returning a local pointing to an allocation */
        int return_slot = -1;
        if (node->as.child && node->as.child->type == NODE_IDENTIFIER) {
            return_slot = resolve_local(current, node->as.child->as.identifier.name, node->as.child->as.identifier.length);
            /* If the function returns a pointer type, the allocation escapes. */
            if (return_slot != -1 && is_pointer_type(current->ret_type)) {
                current->locals[return_slot].holds_alloc = false;
            }
        }

        /* RULE 2: void functions must not return a value */
        if (strcmp(current->ret_type, "void") == 0) {
            if (node->as.child) {
                char buf[512];
                snprintf(buf, sizeof(buf), "void function '%s' must not return a value.", (const char*)current->function->name->chars);
                type_error(node->line, buf);
            }
        } else if (current->ret_type[0]) {
            const char* actual = infer_expr_type(node->as.child);
            
            if (is_pointer_type(current->ret_type)) {
                if (!is_pointer_type(actual) && strcmp(actual, "null") != 0) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "function '%s' declared '%s' but returns non-pointer.", 
                             (const char*)current->function->name->chars, current->ret_type);
                    type_error(node->line, buf);
                }
            } else if (is_pointer_type(actual)) {
                /* Returning pointer from non-pointer return type -> Warning */
                fprintf(stderr, "[Tantrums Warning] line %d: returning pointer from non-pointer return type — caller cannot free this pointer\n", node->line);
            } else if (compile_mode == MODE_STATIC) {
                /* Normal type check in static mode */
                if (!types_compatible(current->ret_type, actual)) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "return type mismatch in '%s': expected '%s' got '%s'.", 
                             (const char*)current->function->name->chars, current->ret_type, actual ? actual : "dynamic");
                    type_error(node->line, buf);
                }
            }
        }

        if (node->as.child) {
            compile_expr(node->as.child);
        } else {
            emit_byte(node->line, OP_NULL);
        }
        
        for (int i = 0; i < current->local_count; i++) {
            if (current->locals[i].holds_alloc) {
                /* Check if this pointer escapes via the return expression before erroring */
                if (node->as.child) {
                    EscapeResult er = {false, false, 0, true};
                    analyze_escape(node->as.child, current->locals[i].name, 0, &er);
                    if (er.use_count > 0) {
                        current->locals[i].holds_alloc = false;
                        continue;
                    }
                }
                char buf[512];
                snprintf(buf, sizeof(buf), "Memory leak detected. Pointer '%s' goes out of scope without being freed.", current->locals[i].name);
                type_error(node->line, buf);
                current->locals[i].holds_alloc = false;
            }
        }
        
        emit_byte(node->line, OP_RETURN);
    } break;

    case NODE_THROW:
        if (current->function->name == nullptr) {
            type_error(node->line, "'throw' statement used outside of a function.");
        }
        compile_expr(node->as.child);
        emit_byte(node->line, OP_THROW);
        break;

    case NODE_FREE:
        if (node->as.child->type == NODE_IDENTIFIER) {
            int slot = resolve_local(current, node->as.child->as.identifier.name, node->as.child->as.identifier.length);
            if (slot != -1) {
                current->locals[slot].holds_alloc = false;
            }
        }
        compile_expr(node->as.child);
        emit_byte(node->line, OP_FREE);
        break;

    case NODE_BREAK:
        if (current_loop == nullptr) {
            type_error(node->line, "'break' used outside of loop.");
            break;
        }
        for (int i = current->local_count - 1; i >= 0 && current->locals[i].depth > current_loop->scope_depth; i--) {
            emit_byte(node->line, OP_POP);
        }
        if (current_loop->break_count < 64) {
            current_loop->breaks[current_loop->break_count++] = emit_jump(node->line, OP_JUMP);
        } else {
            type_error(node->line, "Too many breaks in loop.");
        }
        break;

    case NODE_CONTINUE:
        if (current_loop == nullptr) {
            type_error(node->line, "'continue' used outside of loop.");
            break;
        }
        for (int i = current->local_count - 1; i >= 0 && current->locals[i].depth > current_loop->scope_depth; i--) {
            emit_byte(node->line, OP_POP);
        }
        if (current_loop->type == 0) { /* while loop */
            emit_loop(node->line, current_loop->start);
        } else { /* for_in loop, forward jump to continue target */
            if (current_loop->continue_count < 64) {
                current_loop->continues[current_loop->continue_count++] = emit_jump(node->line, OP_JUMP);
            } else {
                type_error(node->line, "Too many continues in loop.");
            }
        }
        break;


    case NODE_PROGRAM:
        for (int i = 0; i < node->as.program.count; i++)
            compile_node(node->as.program.nodes[i]);
        break;

    case NODE_TRY_CATCH: {
        /*  Layout:
         *    OP_TRY_BEGIN <catch_offset>     ← push handler
         *    [try body]
         *    OP_TRY_END                      ← pop handler
         *    OP_JUMP <end_offset>            ← skip catch block
         *    [catch: error val on stack]
         *    [if err_var: SET_LOCAL, else POP]
         *    [catch body]
         *    [end]
         */
        begin_scope();

        /* OP_TRY_BEGIN — will patch with catch offset */
        int try_begin = current->function->chunk->count;
        emit_byte(node->line, OP_TRY_BEGIN);
        emit_byte(node->line, 0xFF); /* placeholder high */
        emit_byte(node->line, 0xFF); /* placeholder low */

        /* Compile try body */
        compile_node(node->as.try_catch.try_body);

        /* OP_TRY_END — pop exception handler */
        emit_byte(node->line, OP_TRY_END);

        /* Jump past the catch block */
        int skip_catch = emit_jump(node->line, OP_JUMP);

        /* Patch the catch offset: from after OP_TRY_BEGIN to here */
        int catch_start = current->function->chunk->count;
        int offset = catch_start - (try_begin + 3); /* +3 for opcode + 2 operand bytes */
        current->function->chunk->code[try_begin + 1] = (offset >> 8) & 0xFF;
        current->function->chunk->code[try_begin + 2] = offset & 0xFF;

        /* Handle the error value (pushed on stack by OP_THROW) */
        if (node->as.try_catch.err_var) {
            /* Declare error variable as local */
            add_local(node->as.try_catch.err_var,
                      (int)strlen(node->as.try_catch.err_var), "string");
        } else {
            /* No error variable — discard the error value */
            emit_byte(node->line, OP_POP);
        }

        /* Compile catch body */
        compile_node(node->as.try_catch.catch_body);

        end_scope(node->line);

        /* Patch the skip-catch jump */
        patch_jump(skip_catch);
    } break;

    case NODE_USE:
        /* Already resolved before compilation — skip */
        break;

    default:
        compile_expr(node);
        emit_byte(node->line, OP_POP);
        break;
    }
}

ObjFunction* compiler_compile(ASTNode* program, CompileMode mode) {
    had_type_error = false;
    compile_mode = mode;
    global_count = 0;

    /* Pre-scan to collect function signatures for type checking */
    prescan_signatures(program);
    if (!find_func_sig("main")) {
        fprintf(stderr, "Warning: NO MAIN FUNCTION\n");
    }

    ObjFunction* fn = obj_function_new();
    fn->name = nullptr; /* script/top-level */

    CompilerState comp;
    memset(&comp, 0, sizeof(comp));
    comp.function = fn;
    comp.enclosing = nullptr;
    comp.scope_depth = 0;
    current = &comp;
    current_loop = nullptr;

    add_local("", 0); /* slot 0 reserved */

    compile_node(program);

    emit_byte(0, OP_NULL);
    emit_byte(0, OP_RETURN);

    current = nullptr;

    if (had_type_error) {
        fprintf(stderr, "Compilation aborted due to type errors.\n");
        return nullptr;
    }
    return fn;
}