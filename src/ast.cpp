#include "ast.h"

ASTNode* ast_new(NodeType type, int line) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    return node;
}

void nodelist_init(NodeList* list) {
    list->nodes = nullptr;
    list->count = 0;
    list->capacity = 0;
}

void nodelist_add(NodeList* list, ASTNode* node) {
    if (list->count >= list->capacity) {
        int cap = list->capacity < 8 ? 8 : list->capacity * 2;
        list->nodes = (ASTNode**)realloc(list->nodes, sizeof(ASTNode*) * cap);
        list->capacity = cap;
    }
    list->nodes[list->count++] = node;
}

void nodelist_free(NodeList* list) {
    free(list->nodes);
    nodelist_init(list);
}

void ast_free(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
    case NODE_STRING_LIT:
        free(node->as.string_literal.value);
        break;
    case NODE_IDENTIFIER:
        free(node->as.identifier.name);
        break;
    case NODE_LIST_LIT:
        for (int i = 0; i < node->as.list_literal.count; i++)
            ast_free(node->as.list_literal.nodes[i]);
        nodelist_free(&node->as.list_literal);
        break;
    case NODE_MAP_LIT:
        for (int i = 0; i < node->as.map_literal.count; i++) {
            ast_free(node->as.map_literal.keys[i]);
            ast_free(node->as.map_literal.values[i]);
        }
        free(node->as.map_literal.keys);
        free(node->as.map_literal.values);
        break;
    case NODE_UNARY:
        ast_free(node->as.unary.operand);
        break;
    case NODE_BINARY:
        ast_free(node->as.binary.left);
        ast_free(node->as.binary.right);
        break;
    case NODE_ASSIGN:
        free(node->as.assign.name);
        ast_free(node->as.assign.value);
        break;
    case NODE_CALL:
        ast_free(node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++)
            ast_free(node->as.call.args[i]);
        free(node->as.call.args);
        break;
    case NODE_INDEX:
        ast_free(node->as.index_access.object);
        ast_free(node->as.index_access.index);
        break;
    case NODE_INDEX_ASSIGN:
        ast_free(node->as.index_assign.object);
        ast_free(node->as.index_assign.index);
        ast_free(node->as.index_assign.value);
        break;
    case NODE_ALLOC:
        free(node->as.alloc_expr.type_name);
        ast_free(node->as.alloc_expr.init);
        break;
    case NODE_VAR_DECL:
        free(node->as.var_decl.type_name);
        free(node->as.var_decl.name);
        ast_free(node->as.var_decl.init);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < node->as.block.count; i++)
            ast_free(node->as.block.nodes[i]);
        nodelist_free(&node->as.block);
        break;
    case NODE_IF:
        ast_free(node->as.if_stmt.cond);
        ast_free(node->as.if_stmt.then_b);
        ast_free(node->as.if_stmt.else_b);
        break;
    case NODE_WHILE:
        ast_free(node->as.while_stmt.cond);
        ast_free(node->as.while_stmt.body);
        break;
    case NODE_FOR_IN:
        free(node->as.for_in.var_name);
        ast_free(node->as.for_in.iterable);
        ast_free(node->as.for_in.body);
        break;
    case NODE_FUNC_DECL:
        free(node->as.func_decl.name);
        free(node->as.func_decl.ret_type);
        for (int i = 0; i < node->as.func_decl.param_count; i++) {
            free(node->as.func_decl.params[i].name);
            free(node->as.func_decl.params[i].type_name);
        }
        free(node->as.func_decl.params);
        ast_free(node->as.func_decl.body);
        break;
    case NODE_RETURN: case NODE_THROW: case NODE_FREE: case NODE_EXPR_STMT:
        ast_free(node->as.child);
        break;
    case NODE_USE:
        free(node->as.use_file);
        break;
    case NODE_PROGRAM:
        for (int i = 0; i < node->as.program.count; i++)
            ast_free(node->as.program.nodes[i]);
        nodelist_free(&node->as.program);
        break;
    default: break;
    }
    free(node);
}
