#ifndef TANTRUMS_AST_H
#define TANTRUMS_AST_H

#include "common.h"
#include "token.h"

typedef enum {
    /* Expressions */
    NODE_INT_LIT, NODE_FLOAT_LIT, NODE_STRING_LIT,
    NODE_BOOL_LIT, NODE_NULL_LIT,
    NODE_LIST_LIT, NODE_MAP_LIT,
    NODE_IDENTIFIER, NODE_UNARY, NODE_BINARY,
    NODE_ASSIGN, NODE_CALL, NODE_INDEX, NODE_INDEX_ASSIGN,
    NODE_ALLOC, NODE_POSTFIX,

    /* Statements */
    NODE_EXPR_STMT, NODE_VAR_DECL, NODE_BLOCK,
    NODE_IF, NODE_WHILE, NODE_FOR_IN,
    NODE_FUNC_DECL, NODE_RETURN, NODE_THROW, NODE_FREE,
    NODE_USE, NODE_TRY_CATCH, NODE_BREAK, NODE_CONTINUE,
    NODE_PROGRAM,
} NodeType;

typedef struct { char* name; char* type_name; } ParamDef;

typedef struct ASTNode ASTNode;
typedef struct { ASTNode** nodes; int count; int capacity; } NodeList;

struct ASTNode {
    NodeType type;
    int      line;
    union {
        int64_t int_literal;                                          /* INT_LIT   */
        double  float_literal;                                        /* FLOAT_LIT */
        struct { char* value; int length; }          string_literal;  /* STRING_LIT*/
        bool    bool_literal;                                         /* BOOL_LIT  */
        NodeList list_literal;                                        /* LIST_LIT  */
        struct { ASTNode** keys; ASTNode** values; int count; } map_literal; /* MAP_LIT */
        struct { char* name; int length; }           identifier;      /* IDENTIFIER*/
        struct { TokenType op; ASTNode* operand; }   unary;           /* UNARY     */
        struct { TokenType op; ASTNode* left; ASTNode* right; } binary; /* BINARY */
        struct { char* name; ASTNode* value; }       assign;          /* ASSIGN    */
        struct { ASTNode* callee; ASTNode** args; int arg_count; } call; /* CALL   */
        struct { ASTNode* object; ASTNode* index; }  index_access;    /* INDEX     */
        struct { ASTNode* object; ASTNode* index; ASTNode* value; } index_assign; /* INDEX_ASSIGN */
        struct { char* type_name; ASTNode* init; }   alloc_expr;      /* ALLOC     */
        struct { TokenType op; ASTNode* operand; }   postfix;         /* POSTFIX   */
        struct { char* type_name; char* name; ASTNode* init; } var_decl; /* VAR_DECL */
        NodeList block;                                               /* BLOCK     */
        struct { ASTNode* cond; ASTNode* then_b; ASTNode* else_b; } if_stmt;  /* IF */
        struct { ASTNode* cond; ASTNode* body; }     while_stmt;      /* WHILE     */
        struct { char* var_name; ASTNode* iterable; ASTNode* body; } for_in;  /* FOR_IN */
        struct { char* name; char* ret_type; ParamDef* params; int param_count; ASTNode* body; } func_decl;
        ASTNode* child;                                               /* RETURN, THROW, FREE, EXPR_STMT */
        char* use_file;                                                /* USE       */
        struct { ASTNode* try_body; ASTNode* catch_body; char* err_var; } try_catch; /* TRY_CATCH */
        NodeList program;                                             /* PROGRAM   */
    } as;
};

ASTNode* ast_new(NodeType type, int line);
void     ast_free(ASTNode* node);
void     nodelist_init(NodeList* list);
void     nodelist_add(NodeList* list, ASTNode* node);
void     nodelist_free(NodeList* list);

#endif
