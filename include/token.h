#ifndef TANTRUMS_TOKEN_H
#define TANTRUMS_TOKEN_H

#include "common.h"

typedef enum {
    /* Single-character tokens */
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON, TOKEN_COLON,

    /* Arithmetic operators */
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_BANG, TOKEN_AMPERSAND,
    TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,           /* ++ -- */
    TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL,           /* += -= */
    TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL,           /* *= /= */
    TOKEN_PERCENT_EQUAL,                           /* %= */

    /* Comparison / assignment */
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL,
    TOKEN_LESS, TOKEN_GREATER,
    TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
    TOKEN_EQUAL_GREATER, TOKEN_EQUAL_LESS,   /* => and =<  (reversed forms) */

    /* Logical */
    TOKEN_AND, TOKEN_OR,

    /* Literals */
    TOKEN_INT_LITERAL, TOKEN_FLOAT_LITERAL, TOKEN_STRING_LITERAL,
    TOKEN_IDENTIFIER,

    /* Keywords */
    TOKEN_TANTRUM, TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR,
    TOKEN_IN, TOKEN_RETURN, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_ALLOC, TOKEN_FREE, TOKEN_THROW, TOKEN_NULL_KW, TOKEN_USE,
    TOKEN_TRY, TOKEN_CATCH, TOKEN_BREAK, TOKEN_CONTINUE,

    /* Type keywords */
    TOKEN_TYPE_INT, TOKEN_TYPE_FLOAT, TOKEN_TYPE_STRING,
    TOKEN_TYPE_BOOL, TOKEN_TYPE_LIST, TOKEN_TYPE_MAP,
    TOKEN_VOID,

    TOKEN_EOF, TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    Token* tokens;
    int count;
    int capacity;
} TokenList;

void        tokenlist_init(TokenList* list);
void        tokenlist_write(TokenList* list, Token token);
void        tokenlist_free(TokenList* list);
const char* token_type_name(TokenType type);

#endif
