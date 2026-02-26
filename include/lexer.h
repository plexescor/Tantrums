#ifndef TANTRUMS_LEXER_H
#define TANTRUMS_LEXER_H

#include "token.h"

typedef struct {
    const char* source;
    const char* start;
    const char* current;
    int line;
} Lexer;

void      lexer_init(Lexer* lexer, const char* source);
TokenList lexer_scan_tokens(Lexer* lexer);

#endif
