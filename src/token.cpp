#include "token.h"

void tokenlist_init(TokenList* list) {
    list->tokens = nullptr;
    list->count = 0;
    list->capacity = 0;
}

void tokenlist_write(TokenList* list, Token token) {
    if (list->count >= list->capacity) {
        int cap = list->capacity < 8 ? 8 : list->capacity * 2;
        list->tokens = (Token*)realloc(list->tokens, sizeof(Token) * cap);
        list->capacity = cap;
    }
    list->tokens[list->count++] = token;
}

void tokenlist_free(TokenList* list) {
    free(list->tokens);
    tokenlist_init(list);
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_LEFT_PAREN:    return "(";
        case TOKEN_RIGHT_PAREN:   return ")";
        case TOKEN_LEFT_BRACE:    return "{";
        case TOKEN_RIGHT_BRACE:   return "}";
        case TOKEN_LEFT_BRACKET:  return "[";
        case TOKEN_RIGHT_BRACKET: return "]";
        case TOKEN_COMMA:         return ",";
        case TOKEN_DOT:           return ".";
        case TOKEN_SEMICOLON:     return ";";
        case TOKEN_COLON:         return ":";
        case TOKEN_PLUS:          return "+";
        case TOKEN_MINUS:         return "-";
        case TOKEN_STAR:          return "*";
        case TOKEN_SLASH:         return "/";
        case TOKEN_PERCENT:       return "%";
        case TOKEN_BANG:          return "!";
        case TOKEN_AMPERSAND:     return "&";
        case TOKEN_EQUAL:         return "=";
        case TOKEN_EQUAL_EQUAL:   return "==";
        case TOKEN_BANG_EQUAL:    return "!=";
        case TOKEN_LESS:          return "<";
        case TOKEN_GREATER:       return ">";
        case TOKEN_LESS_EQUAL:    return "<=";
        case TOKEN_GREATER_EQUAL: return ">=";
        case TOKEN_EQUAL_GREATER: return "=>";
        case TOKEN_EQUAL_LESS:    return "=<";
        case TOKEN_AND:           return "&&";
        case TOKEN_OR:            return "||";
        case TOKEN_INT_LITERAL:   return "INT";
        case TOKEN_FLOAT_LITERAL: return "FLOAT";
        case TOKEN_STRING_LITERAL:return "STRING";
        case TOKEN_IDENTIFIER:    return "IDENT";
        case TOKEN_TANTRUM:       return "tantrum";
        case TOKEN_IF:            return "if";
        case TOKEN_ELSE:          return "else";
        case TOKEN_WHILE:         return "while";
        case TOKEN_FOR:           return "for";
        case TOKEN_IN:            return "in";
        case TOKEN_RETURN:        return "return";
        case TOKEN_TRUE:          return "true";
        case TOKEN_FALSE:         return "false";
        case TOKEN_ALLOC:         return "alloc";
        case TOKEN_FREE:          return "free";
        case TOKEN_THROW:         return "throw";
        case TOKEN_NULL_KW:       return "null";
        case TOKEN_TYPE_INT:      return "int";
        case TOKEN_TYPE_FLOAT:    return "float";
        case TOKEN_TYPE_STRING:   return "string";
        case TOKEN_TYPE_BOOL:     return "bool";
        case TOKEN_TYPE_LIST:     return "list";
        case TOKEN_TYPE_MAP:      return "map";
        case TOKEN_VOID:          return "void";
        case TOKEN_EOF:           return "EOF";
        case TOKEN_ERROR:         return "ERROR";
    }
    return "?";
}
