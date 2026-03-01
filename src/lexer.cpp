#include "lexer.h"
#include <cstring>
#include <cstdlib>

void lexer_init(Lexer* lex, const char* source) {
    lex->source = source;
    lex->start = source;
    lex->current = source;
    lex->line = 1;
}

static bool is_at_end(Lexer* l) { return *l->current == '\0'; }
static char advance(Lexer* l) { return *l->current++; }
static char peek(Lexer* l) { return *l->current; }
static char peek_next(Lexer* l) { return l->current[1]; }

static bool match(Lexer* l, char expected) {
    if (is_at_end(l) || *l->current != expected) return false;
    l->current++; return true;
}

static Token make_token(Lexer* l, TokenType type) {
    Token t;
    t.type = type;
    t.start = l->start;
    t.length = (int)(l->current - l->start);
    t.line = l->line;
    return t;
}

static Token error_token(Lexer* l, const char* msg) {
    Token t;
    t.type = TOKEN_ERROR;
    t.start = msg;
    t.length = (int)strlen(msg);
    t.line = l->line;
    return t;
}

static void skip_whitespace(Lexer* l) {
    for (;;) {
        char c = peek(l);
        if (c == ' ' || c == '\r' || c == '\t') { advance(l); }
        else if (c == '\n') { l->line++; advance(l); }
        else if (c == '/' && peek_next(l) == '/') {
            while (!is_at_end(l) && peek(l) != '\n') advance(l);
        } else if (c == '/' && peek_next(l) == '*') {
            advance(l); advance(l);
            while (!is_at_end(l)) {
                if (peek(l) == '\n') l->line++;
                if (peek(l) == '*' && peek_next(l) == '/') { advance(l); advance(l); break; }
                advance(l);
            }
        } else return;
    }
}

static TokenType check_keyword(Lexer* l, int start, int len, const char* rest, TokenType type) {
    if ((int)(l->current - l->start) == start + len && memcmp(l->start + start, rest, len) == 0)
        return type;
    return TOKEN_IDENTIFIER;
}

static TokenType identifier_type(Lexer* l) {
    int len = (int)(l->current - l->start);
    switch (l->start[0]) {
    case 'a':
        if (len == 5 && memcmp(l->start, "alloc", 5) == 0) return TOKEN_ALLOC;
        if (len == 3 && memcmp(l->start, "and", 3) == 0) return TOKEN_AND;
        break;
    case 'b':
        if (len == 4 && memcmp(l->start, "bool", 4) == 0) return TOKEN_TYPE_BOOL;
        if (len == 5 && memcmp(l->start, "break", 5) == 0) return TOKEN_BREAK;
        break;
    case 'c':
        if (len == 5 && memcmp(l->start, "catch", 5) == 0) return TOKEN_CATCH;
        if (len == 8 && memcmp(l->start, "continue", 8) == 0) return TOKEN_CONTINUE;
        break;
    case 'e': return check_keyword(l, 1, 3, "lse", TOKEN_ELSE);
    case 'f':
        if (len > 1) {
            switch (l->start[1]) {
            case 'a': return check_keyword(l, 2, 3, "lse", TOKEN_FALSE);
            case 'l': return check_keyword(l, 2, 3, "oat", TOKEN_TYPE_FLOAT);
            case 'o': return check_keyword(l, 2, 1, "r", TOKEN_FOR);
            case 'r': return check_keyword(l, 2, 2, "ee", TOKEN_FREE);
            }
        } break;
    case 'i':
        if (len == 2 && l->start[1] == 'f') return TOKEN_IF;
        if (len == 2 && l->start[1] == 'n') return TOKEN_IN;
        if (len == 3 && memcmp(l->start, "int", 3) == 0) return TOKEN_TYPE_INT;
        break;
    case 'l': return check_keyword(l, 1, 3, "ist", TOKEN_TYPE_LIST);
    case 'm': return check_keyword(l, 1, 2, "ap", TOKEN_TYPE_MAP);
    case 'n': return check_keyword(l, 1, 3, "ull", TOKEN_NULL_KW);
    case 'o':
        if (len == 2 && l->start[1] == 'r') return TOKEN_OR;
        break;
    case 'r': return check_keyword(l, 1, 5, "eturn", TOKEN_RETURN);
    case 's': return check_keyword(l, 1, 5, "tring", TOKEN_TYPE_STRING);
    case 't':
        if (len > 1) {
            switch (l->start[1]) {
            case 'a': return check_keyword(l, 2, 5, "ntrum", TOKEN_TANTRUM);
            case 'h': return check_keyword(l, 2, 3, "row", TOKEN_THROW);
            case 'r': if (len == 4) return check_keyword(l, 2, 2, "ue", TOKEN_TRUE);
                      if (len == 3) return check_keyword(l, 2, 1, "y", TOKEN_TRY);
                      break;
            }
        } break;
    case 'u': return check_keyword(l, 1, 2, "se", TOKEN_USE);
    case 'v': return check_keyword(l, 1, 3, "oid", TOKEN_VOID);
    case 'w': return check_keyword(l, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token scan_string(Lexer* l) {
    while (!is_at_end(l) && peek(l) != '"') {
        if (peek(l) == '\n') l->line++;
        if (peek(l) == '\\') {
            advance(l); /* skip backslash */
            if (is_at_end(l)) return error_token(l, "Unterminated string.");
            
            char escape = peek(l);
            if (escape != 'n' && escape != 't' && escape != '\\' && 
                escape != '"' && escape != 'r' && escape != '0') {
                return error_token(l, "Invalid escape sequence.");
            }
        }
        advance(l);
    }
    if (is_at_end(l)) return error_token(l, "Unterminated string.");
    advance(l); /* closing " */
    return make_token(l, TOKEN_STRING_LITERAL);
}

static Token scan_number(Lexer* l) {
    while (!is_at_end(l) && isdigit(peek(l))) advance(l);
    bool is_float = false;
    if (peek(l) == '.' && isdigit(peek_next(l))) {
        is_float = true;
        advance(l);
        while (!is_at_end(l) && isdigit(peek(l))) advance(l);
    }
    return make_token(l, is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL);
}

static Token scan_identifier(Lexer* l) {
    while (!is_at_end(l) && (isalnum(peek(l)) || peek(l) == '_')) advance(l);
    return make_token(l, identifier_type(l));
}

static Token scan_token(Lexer* l) {
    skip_whitespace(l);
    l->start = l->current;
    if (is_at_end(l)) return make_token(l, TOKEN_EOF);

    char c = advance(l);

    if (isalpha(c) || c == '_') return scan_identifier(l);
    if (isdigit(c)) return scan_number(l);

    switch (c) {
    case '(': return make_token(l, TOKEN_LEFT_PAREN);
    case ')': return make_token(l, TOKEN_RIGHT_PAREN);
    case '{': return make_token(l, TOKEN_LEFT_BRACE);
    case '}': return make_token(l, TOKEN_RIGHT_BRACE);
    case '[': return make_token(l, TOKEN_LEFT_BRACKET);
    case ']': return make_token(l, TOKEN_RIGHT_BRACKET);
    case ',': return make_token(l, TOKEN_COMMA);
    case '.': return make_token(l, TOKEN_DOT);
    case ';': return make_token(l, TOKEN_SEMICOLON);
    case ':': return make_token(l, TOKEN_COLON);
    case '+':
        if (match(l, '+')) return make_token(l, TOKEN_PLUS_PLUS);
        if (match(l, '=')) return make_token(l, TOKEN_PLUS_EQUAL);
        return make_token(l, TOKEN_PLUS);
    case '-':
        if (match(l, '-')) return make_token(l, TOKEN_MINUS_MINUS);
        if (match(l, '=')) return make_token(l, TOKEN_MINUS_EQUAL);
        return make_token(l, TOKEN_MINUS);
    case '*':
        if (match(l, '=')) return make_token(l, TOKEN_STAR_EQUAL);
        return make_token(l, TOKEN_STAR);
    case '/':
        if (match(l, '=')) return make_token(l, TOKEN_SLASH_EQUAL);
        return make_token(l, TOKEN_SLASH);
    case '%':
        if (match(l, '=')) return make_token(l, TOKEN_PERCENT_EQUAL);
        return make_token(l, TOKEN_PERCENT);
    case '&': return make_token(l, match(l, '&') ? TOKEN_AND : TOKEN_AMPERSAND);
    case '|': if (match(l, '|')) return make_token(l, TOKEN_OR);
              return error_token(l, "Expected '||'.");
    case '!': return make_token(l, match(l, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
        if (match(l, '=')) return make_token(l, TOKEN_EQUAL_EQUAL);
        if (match(l, '>')) return make_token(l, TOKEN_EQUAL_GREATER);
        if (match(l, '<')) return make_token(l, TOKEN_EQUAL_LESS);
        return make_token(l, TOKEN_EQUAL);
    case '<': return make_token(l, match(l, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>': return make_token(l, match(l, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return scan_string(l);
    case '#': {
        while (!is_at_end(l) && isalpha(peek(l))) advance(l);
        int len = (int)(l->current - l->start);
        if (len == 9 && memcmp(l->start, "#autoFree", 9) == 0) {
            return make_token(l, TOKEN_AUTOFREE_KW);
        }
        if (len == 17 && memcmp(l->start, "#allowMemoryLeaks", 17) == 0) {
            return make_token(l, TOKEN_ALLOW_LEAKS_KW);
        }
        /* #mode is pre-stripped by main.cpp before lexing, but handle gracefully in case */
        if (len == 5 && memcmp(l->start, "#mode", 5) == 0) {
            while (!is_at_end(l) && peek(l) != '\n') advance(l);
            return scan_token(l);
        }
        return error_token(l, "Unknown directive.");
    }
    }
    return error_token(l, "Unexpected character.");
}

TokenList lexer_scan_tokens(Lexer* l) {
    TokenList list;
    tokenlist_init(&list);
    for (;;) {
        Token t = scan_token(l);
        tokenlist_write(&list, t);
        if (t.type == TOKEN_EOF || t.type == TOKEN_ERROR) break;
    }
    return list;
}