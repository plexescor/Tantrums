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
} Local;

typedef struct CompilerState {
    ObjFunction*        function;
    struct CompilerState* enclosing;
    Local  locals[MAX_LOCALS];
    int    local_count;
    int    scope_depth;
} CompilerState;

static CompilerState* current = nullptr;
static bool had_type_error = false;
static CompileMode compile_mode = MODE_BOTH;

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

static bool types_compatible(const char* expected, const char* actual) {
    if (!expected || !expected[0] || !actual) return true; /* dynamic = always OK */
    if (strcmp(expected, actual) == 0) return true;
    /* int and float are promotable */
    if (strcmp(expected, "float") == 0 && strcmp(actual, "int") == 0) return true;
    return false;
}

static void type_error(int line, const char* msg) {
    fprintf(stderr, "[Line %d] Type Error: %s\n", line, msg);
    had_type_error = true;
}

/* Check function call argument types */
static void check_call_types(ASTNode* call_node) {
    if (call_node->as.call.callee->type != NODE_IDENTIFIER) return;
    const char* fn_name = call_node->as.call.callee->as.identifier.name;
    
    /* Skip builtin validation */
    if (strcmp(fn_name, "print") == 0 || strcmp(fn_name, "input") == 0 ||
    strcmp(fn_name, "len") == 0 || strcmp(fn_name, "range") == 0 ||
    strcmp(fn_name, "type") == 0 || strcmp(fn_name, "append") == 0 ||
    strcmp(fn_name, "getCurrentTime") == 0 || strcmp(fn_name, "toSeconds") == 0 ||
    strcmp(fn_name, "toMilliseconds") == 0 || strcmp(fn_name, "toMinutes") == 0 ||
    strcmp(fn_name, "toHours") == 0 ||
    strcmp(fn_name, "getProcessMemory") == 0 || strcmp(fn_name, "getVmMemory") == 0 ||
    strcmp(fn_name, "getVmPeakMemory") == 0 || strcmp(fn_name, "bytesToKB") == 0 ||
    strcmp(fn_name, "bytesToMB") == 0 || strcmp(fn_name, "bytesToGB") == 0) return;

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
static void begin_scope()  { current->scope_depth++; }
static void end_scope(int line) {
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        Local* local = &current->locals[current->local_count - 1];
        
        bool is_param = (local->depth == 1 && current->function->name != nullptr);
        bool is_in_loop = (local->depth >= 2);
        bool is_hidden = (local->name[0] == '\0' || memcmp(local->name, "$", 1) == 0);

        if (!local->is_used && current->function->name != nullptr && !is_hidden && !is_param && !is_in_loop) {
            fprintf(stderr, "[Line %d] Warning: Unused variable '%s'.\n", line, local->name);
        }
        emit_byte(line, OP_POP);
        current->local_count--;
    }
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
        compile_expr(node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++)
            compile_expr(node->as.call.args[i]);
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

    case NODE_ALLOC:
        compile_expr(node->as.alloc_expr.init);
        emit_byte(node->line, OP_ALLOC);
        break;

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

static void compile_node(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
    case NODE_EXPR_STMT:
        compile_expr(node->as.child);
        emit_byte(node->line, OP_POP);
        break;

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
            add_local(node->as.var_decl.name, (int)strlen(node->as.var_decl.name),
                      node->as.var_decl.type_name);
        } else {
            track_global(node->as.var_decl.name);
            ObjString* name = obj_string_new(node->as.var_decl.name, (int)strlen(node->as.var_decl.name));
            emit_bytes(node->line, OP_DEFINE_GLOBAL, make_constant(OBJ_VAL(name)));
        }
    } break;


    case NODE_BLOCK:
        if (node->as.block.count == 0 && compile_mode != MODE_DYNAMIC) { /* Optional empty block warning */
            /* Ignore empty body warning by default to reduce noise */
        }
        begin_scope();
        for (int i = 0; i < node->as.block.count; i++) {
            compile_node(node->as.block.nodes[i]);
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
        compile_node(node->as.if_stmt.then_b);
        int else_jump = emit_jump(node->line, OP_JUMP);
        patch_jump(then_jump);
        emit_byte(node->line, OP_POP);
        if (node->as.if_stmt.else_b) compile_node(node->as.if_stmt.else_b);
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
        current = &comp;

        /* Reserve slot 0 for the function itself */
        add_local("", 0);

        /* Parameters as locals */
        for (int i = 0; i < node->as.func_decl.param_count; i++) {
            add_local(node->as.func_decl.params[i].name,
                      (int)strlen(node->as.func_decl.params[i].name));
        }

        /* Compile body (it's a block, but don't add extra scope) */
        ASTNode* body = node->as.func_decl.body;
        bool has_return = has_guaranteed_return(body);
        compile_node(body);

        /* Verify non-void function return */
        if (!has_return && node->as.func_decl.ret_type && 
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
        if (node->as.child)
            compile_expr(node->as.child);
        else
            emit_byte(node->line, OP_NULL);
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
        fprintf(stderr, "Warning: NO MAIN FUNC\n");
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
