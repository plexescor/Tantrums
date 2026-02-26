#include "parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef struct {
    TokenList* tokens;
    int        current;
    bool       had_error;
} Parser;

/* ── Helpers ──────────────────────────────────────── */
static Token* peek_tok(Parser* p)    { return &p->tokens->tokens[p->current]; }
static Token* previous(Parser* p)    { return &p->tokens->tokens[p->current - 1]; }
static bool   is_at_end(Parser* p)   { return peek_tok(p)->type == TOKEN_EOF; }

static Token* advance_tok(Parser* p) {
    if (!is_at_end(p)) p->current++;
    return previous(p);
}

static bool check(Parser* p, TokenType t) { return !is_at_end(p) && peek_tok(p)->type == t; }

static bool match(Parser* p, TokenType t) {
    if (!check(p, t)) return false;
    advance_tok(p); return true;
}

static Token* consume(Parser* p, TokenType t, const char* msg) {
    if (check(p, t)) return advance_tok(p);
    fprintf(stderr, "[Line %d] Error: %s (got '%.*s')\n",
            peek_tok(p)->line, msg, peek_tok(p)->length, peek_tok(p)->start);
    p->had_error = true;
    return peek_tok(p);
}

static char* copy_lexeme(Token* t) {
    char* s = (char*)malloc(t->length + 1);
    memcpy(s, t->start, t->length);
    s[t->length] = '\0';
    return s;
}

static char* copy_lexeme_str(const char* str, int len) {
    char* s = (char*)malloc(len + 1);
    memcpy(s, str, len);
    s[len] = '\0';
    return s;
}

/* ── Forward declarations ─────────────────────────── */
static ASTNode* expression(Parser* p);
static ASTNode* statement(Parser* p);
static ASTNode* declaration(Parser* p);

/* ── Expressions (precedence climbing) ────────────── */
static ASTNode* primary(Parser* p) {
    if (match(p, TOKEN_INT_LITERAL)) {
        ASTNode* n = ast_new(NODE_INT_LIT, previous(p)->line);
        char* s = copy_lexeme(previous(p));
        n->as.int_literal = strtoll(s, nullptr, 10);
        free(s);
        return n;
    }
    if (match(p, TOKEN_FLOAT_LITERAL)) {
        ASTNode* n = ast_new(NODE_FLOAT_LIT, previous(p)->line);
        char* s = copy_lexeme(previous(p));
        n->as.float_literal = strtod(s, nullptr);
        free(s);
        return n;
    }
    if (match(p, TOKEN_STRING_LITERAL)) {
        Token* t = previous(p);
        ASTNode* n = ast_new(NODE_STRING_LIT, t->line);
        /* Process escape sequences */
        const char* src = t->start + 1;  /* skip opening quote */
        int src_len = t->length - 2;     /* exclude both quotes */
        char* buf = (char*)malloc(src_len + 1);
        int out = 0;
        for (int i = 0; i < src_len; i++) {
            if (src[i] == '\\' && i + 1 < src_len) {
                i++;
                switch (src[i]) {
                case 'n':  buf[out++] = '\n'; break;
                case 't':  buf[out++] = '\t'; break;
                case 'r':  buf[out++] = '\r'; break;
                case '\\': buf[out++] = '\\'; break;
                case '"':  buf[out++] = '"';  break;
                case '0':  buf[out++] = '\0'; break;
                default:   buf[out++] = '\\'; buf[out++] = src[i]; break;
                }
            } else {
                buf[out++] = src[i];
            }
        }
        buf[out] = '\0';
        n->as.string_literal.value = buf;
        n->as.string_literal.length = out;
        return n;
    }
    if (match(p, TOKEN_TRUE))    { ASTNode* n = ast_new(NODE_BOOL_LIT, previous(p)->line); n->as.bool_literal = true; return n; }
    if (match(p, TOKEN_FALSE))   { ASTNode* n = ast_new(NODE_BOOL_LIT, previous(p)->line); n->as.bool_literal = false; return n; }
    if (match(p, TOKEN_NULL_KW)) { return ast_new(NODE_NULL_LIT, previous(p)->line); }

    if (match(p, TOKEN_IDENTIFIER)) {
        ASTNode* n = ast_new(NODE_IDENTIFIER, previous(p)->line);
        n->as.identifier.name = copy_lexeme(previous(p));
        n->as.identifier.length = previous(p)->length;
        return n;
    }
    if (match(p, TOKEN_LEFT_PAREN)) {
        ASTNode* expr = expression(p);
        consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
        return expr;
    }
    /* List literal [a, b, c] */
    if (match(p, TOKEN_LEFT_BRACKET)) {
        ASTNode* n = ast_new(NODE_LIST_LIT, previous(p)->line);
        nodelist_init(&n->as.list_literal);
        if (!check(p, TOKEN_RIGHT_BRACKET)) {
            do { nodelist_add(&n->as.list_literal, expression(p)); } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RIGHT_BRACKET, "Expected ']'.");
        return n;
    }
    /* Map literal {key: val, ...} */
    if (match(p, TOKEN_LEFT_BRACE)) {
        if (check(p, TOKEN_STRING_LITERAL) || check(p, TOKEN_IDENTIFIER)) {
            ASTNode* n = ast_new(NODE_MAP_LIT, previous(p)->line);
            n->as.map_literal.keys = nullptr;
            n->as.map_literal.values = nullptr;
            n->as.map_literal.count = 0;
            int cap = 0;
            do {
                ASTNode* key = expression(p);
                consume(p, TOKEN_COLON, "Expected ':' in map literal.");
                ASTNode* val = expression(p);
                if (n->as.map_literal.count >= cap) {
                    cap = cap < 4 ? 4 : cap * 2;
                    n->as.map_literal.keys = (ASTNode**)realloc(n->as.map_literal.keys, sizeof(ASTNode*) * cap);
                    n->as.map_literal.values = (ASTNode**)realloc(n->as.map_literal.values, sizeof(ASTNode*) * cap);
                }
                n->as.map_literal.keys[n->as.map_literal.count] = key;
                n->as.map_literal.values[n->as.map_literal.count] = val;
                n->as.map_literal.count++;
            } while (match(p, TOKEN_COMMA));
            consume(p, TOKEN_RIGHT_BRACE, "Expected '}'.");
            return n;
        }
        /* Empty map or error — treat as empty block... but return empty map */
        consume(p, TOKEN_RIGHT_BRACE, "Expected '}'.");
        ASTNode* n = ast_new(NODE_MAP_LIT, previous(p)->line);
        n->as.map_literal.keys = nullptr; n->as.map_literal.values = nullptr; n->as.map_literal.count = 0;
        return n;
    }
    /* alloc type(value) */
    if (match(p, TOKEN_ALLOC)) {
        ASTNode* n = ast_new(NODE_ALLOC, previous(p)->line);
        Token* t = consume(p, TOKEN_IDENTIFIER, "Expected type after 'alloc'.");
        n->as.alloc_expr.type_name = copy_lexeme(t);
        consume(p, TOKEN_LEFT_PAREN, "Expected '(' after alloc type.");
        n->as.alloc_expr.init = expression(p);
        consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
        return n;
    }

    fprintf(stderr, "[Line %d] Error: Unexpected token '%.*s'.\n",
            peek_tok(p)->line, peek_tok(p)->length, peek_tok(p)->start);
    p->had_error = true;
    advance_tok(p);
    return ast_new(NODE_NULL_LIT, previous(p)->line);
}

/* ── Call / Index ─────────────────────────────────── */
static ASTNode* call_expr(Parser* p) {
    ASTNode* expr = primary(p);
    for (;;) {
        if (match(p, TOKEN_LEFT_PAREN)) {
            ASTNode* call = ast_new(NODE_CALL, previous(p)->line);
            call->as.call.callee = expr;
            call->as.call.args = nullptr;
            call->as.call.arg_count = 0;
            int cap = 0;
            if (!check(p, TOKEN_RIGHT_PAREN)) {
                do {
                    if (call->as.call.arg_count >= cap) {
                        cap = cap < 4 ? 4 : cap * 2;
                        call->as.call.args = (ASTNode**)realloc(call->as.call.args, sizeof(ASTNode*) * cap);
                    }
                    call->as.call.args[call->as.call.arg_count++] = expression(p);
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
            expr = call;
        } else if (match(p, TOKEN_LEFT_BRACKET)) {
            ASTNode* idx = ast_new(NODE_INDEX, previous(p)->line);
            idx->as.index_access.object = expr;
            idx->as.index_access.index = expression(p);
            consume(p, TOKEN_RIGHT_BRACKET, "Expected ']'.");
            expr = idx;
        } else break;
    }
    return expr;
}

/* ── Unary ────────────────────────────────────────── */
static ASTNode* unary(Parser* p) {
    if (match(p, TOKEN_BANG) || match(p, TOKEN_MINUS) || match(p, TOKEN_AMPERSAND) || match(p, TOKEN_STAR)) {
        TokenType op = previous(p)->type;
        ASTNode* n = ast_new(NODE_UNARY, previous(p)->line);
        n->as.unary.op = op;
        n->as.unary.operand = unary(p);
        return n;
    }
    /* Prefix ++i / --i → desugar to (i = i + 1) */
    if (match(p, TOKEN_PLUS_PLUS) || match(p, TOKEN_MINUS_MINUS)) {
        bool is_inc = previous(p)->type == TOKEN_PLUS_PLUS;
        int line = previous(p)->line;
        Token* ident = consume(p, TOKEN_IDENTIFIER, "Expected variable after '++' / '--'.");
        /* Build: ident = ident +/- 1 */
        ASTNode* var_ref = ast_new(NODE_IDENTIFIER, line);
        var_ref->as.identifier.name = copy_lexeme(ident);
        var_ref->as.identifier.length = ident->length;
        ASTNode* one = ast_new(NODE_INT_LIT, line);
        one->as.int_literal = 1;
        ASTNode* bin = ast_new(NODE_BINARY, line);
        bin->as.binary.op = is_inc ? TOKEN_PLUS : TOKEN_MINUS;
        bin->as.binary.left = var_ref;
        bin->as.binary.right = one;
        ASTNode* assign = ast_new(NODE_ASSIGN, line);
        assign->as.assign.name = copy_lexeme(ident);
        assign->as.assign.value = bin;
        return assign;
    }
    return call_expr(p);
}

/* ── Binary helpers (precedence levels) ───────────── */
static ASTNode* factor(Parser* p) {
    ASTNode* left = unary(p);
    while (match(p, TOKEN_STAR) || match(p, TOKEN_SLASH) || match(p, TOKEN_PERCENT)) {
        TokenType op = previous(p)->type;
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = op; n->as.binary.left = left; n->as.binary.right = unary(p);
        left = n;
    }
    return left;
}

static ASTNode* term(Parser* p) {
    ASTNode* left = factor(p);
    while (match(p, TOKEN_PLUS) || match(p, TOKEN_MINUS)) {
        TokenType op = previous(p)->type;
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = op; n->as.binary.left = left; n->as.binary.right = factor(p);
        left = n;
    }
    return left;
}

static ASTNode* comparison(Parser* p) {
    ASTNode* left = term(p);
    while (match(p, TOKEN_LESS) || match(p, TOKEN_GREATER) ||
           match(p, TOKEN_LESS_EQUAL) || match(p, TOKEN_GREATER_EQUAL) ||
           match(p, TOKEN_EQUAL_GREATER) || match(p, TOKEN_EQUAL_LESS)) {
        TokenType op = previous(p)->type;
        /* Normalize reversed operators */
        if (op == TOKEN_EQUAL_GREATER) op = TOKEN_GREATER_EQUAL;
        if (op == TOKEN_EQUAL_LESS)    op = TOKEN_LESS_EQUAL;
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = op; n->as.binary.left = left; n->as.binary.right = term(p);
        left = n;
    }
    return left;
}

static ASTNode* equality(Parser* p) {
    ASTNode* left = comparison(p);
    while (match(p, TOKEN_EQUAL_EQUAL) || match(p, TOKEN_BANG_EQUAL)) {
        TokenType op = previous(p)->type;
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = op; n->as.binary.left = left; n->as.binary.right = comparison(p);
        left = n;
    }
    return left;
}

static ASTNode* logic_and(Parser* p) {
    ASTNode* left = equality(p);
    while (match(p, TOKEN_AND)) {
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = TOKEN_AND; n->as.binary.left = left; n->as.binary.right = equality(p);
        left = n;
    }
    return left;
}

static ASTNode* logic_or(Parser* p) {
    ASTNode* left = logic_and(p);
    while (match(p, TOKEN_OR)) {
        ASTNode* n = ast_new(NODE_BINARY, previous(p)->line);
        n->as.binary.op = TOKEN_OR; n->as.binary.left = left; n->as.binary.right = logic_and(p);
        left = n;
    }
    return left;
}

static ASTNode* expression(Parser* p) {
    return logic_or(p);
}

/* ── Statements ───────────────────────────────────── */
static ASTNode* block(Parser* p) {
    ASTNode* n = ast_new(NODE_BLOCK, previous(p)->line);
    nodelist_init(&n->as.block);
    while (!check(p, TOKEN_RIGHT_BRACE) && !is_at_end(p)) {
        nodelist_add(&n->as.block, declaration(p));
    }
    consume(p, TOKEN_RIGHT_BRACE, "Expected '}'.");
    return n;
}

static bool is_type_token(TokenType t) {
    return t == TOKEN_TYPE_INT || t == TOKEN_TYPE_FLOAT || t == TOKEN_TYPE_STRING ||
           t == TOKEN_TYPE_BOOL || t == TOKEN_TYPE_LIST || t == TOKEN_TYPE_MAP;
}

static ASTNode* if_statement(Parser* p) {
    ASTNode* n = ast_new(NODE_IF, previous(p)->line);
    consume(p, TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
    n->as.if_stmt.cond = expression(p);
    consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
    consume(p, TOKEN_LEFT_BRACE, "Expected '{'.");
    n->as.if_stmt.then_b = block(p);
    n->as.if_stmt.else_b = nullptr;
    if (match(p, TOKEN_ELSE)) {
        if (match(p, TOKEN_IF)) {
            n->as.if_stmt.else_b = if_statement(p);
        } else {
            consume(p, TOKEN_LEFT_BRACE, "Expected '{'.");
            n->as.if_stmt.else_b = block(p);
        }
    }
    return n;
}

static ASTNode* while_statement(Parser* p) {
    ASTNode* n = ast_new(NODE_WHILE, previous(p)->line);
    consume(p, TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
    n->as.while_stmt.cond = expression(p);
    consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
    consume(p, TOKEN_LEFT_BRACE, "Expected '{'.");
    n->as.while_stmt.body = block(p);
    return n;
}

static ASTNode* for_statement(Parser* p) {
    ASTNode* n = ast_new(NODE_FOR_IN, previous(p)->line);
    Token* var = consume(p, TOKEN_IDENTIFIER, "Expected variable name after 'for'.");
    n->as.for_in.var_name = copy_lexeme(var);
    consume(p, TOKEN_IN, "Expected 'in' after variable in for loop.");
    n->as.for_in.iterable = expression(p);
    consume(p, TOKEN_LEFT_BRACE, "Expected '{'.");
    n->as.for_in.body = block(p);
    return n;
}

static ASTNode* func_declaration(Parser* p) {
    ASTNode* n = ast_new(NODE_FUNC_DECL, previous(p)->line);
    n->as.func_decl.ret_type = nullptr;
    /* Check for optional return type */
    if (is_type_token(peek_tok(p)->type)) {
        Token* rt = advance_tok(p);
        n->as.func_decl.ret_type = copy_lexeme(rt);
    }
    Token* name = consume(p, TOKEN_IDENTIFIER, "Expected function name.");
    n->as.func_decl.name = copy_lexeme(name);
    consume(p, TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    n->as.func_decl.params = nullptr;
    n->as.func_decl.param_count = 0;
    int cap = 0;
    if (!check(p, TOKEN_RIGHT_PAREN)) {
        do {
            ParamDef param;
            param.type_name = nullptr;
            /* Optional type annotation */
            if (is_type_token(peek_tok(p)->type)) {
                Token* pt = advance_tok(p);
                param.type_name = copy_lexeme(pt);
            }
            Token* pn = consume(p, TOKEN_IDENTIFIER, "Expected parameter name.");
            param.name = copy_lexeme(pn);
            if (n->as.func_decl.param_count >= cap) {
                cap = cap < 4 ? 4 : cap * 2;
                n->as.func_decl.params = (ParamDef*)realloc(n->as.func_decl.params, sizeof(ParamDef) * cap);
            }
            n->as.func_decl.params[n->as.func_decl.param_count++] = param;
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RIGHT_PAREN, "Expected ')'.");
    consume(p, TOKEN_LEFT_BRACE, "Expected '{'.");
    n->as.func_decl.body = block(p);
    return n;
}

static ASTNode* statement(Parser* p) {
    if (match(p, TOKEN_IF))     return if_statement(p);
    if (match(p, TOKEN_WHILE))  return while_statement(p);
    if (match(p, TOKEN_FOR))    return for_statement(p);
    if (match(p, TOKEN_RETURN)) {
        ASTNode* n = ast_new(NODE_RETURN, previous(p)->line);
        n->as.child = check(p, TOKEN_SEMICOLON) ? nullptr : expression(p);
        consume(p, TOKEN_SEMICOLON, "Expected ';' after return.");
        return n;
    }
    if (match(p, TOKEN_THROW)) {
        ASTNode* n = ast_new(NODE_THROW, previous(p)->line);
        n->as.child = expression(p);
        consume(p, TOKEN_SEMICOLON, "Expected ';' after throw.");
        return n;
    }
    if (match(p, TOKEN_FREE)) {
        ASTNode* n = ast_new(NODE_FREE, previous(p)->line);
        n->as.child = expression(p);
        consume(p, TOKEN_SEMICOLON, "Expected ';' after free.");
        return n;
    }
    /* Expression statement (including assignments) */
    ASTNode* expr = expression(p);

    /* Postfix i++ / i-- */
    if (expr->type == NODE_IDENTIFIER &&
        (check(p, TOKEN_PLUS_PLUS) || check(p, TOKEN_MINUS_MINUS))) {
        bool is_inc = peek_tok(p)->type == TOKEN_PLUS_PLUS;
        advance_tok(p);
        /* Desugar: i = i +/- 1 */
        ASTNode* var_ref = ast_new(NODE_IDENTIFIER, expr->line);
        var_ref->as.identifier.name = copy_lexeme_str(expr->as.identifier.name, (int)strlen(expr->as.identifier.name));
        var_ref->as.identifier.length = (int)strlen(expr->as.identifier.name);
        ASTNode* one = ast_new(NODE_INT_LIT, expr->line);
        one->as.int_literal = 1;
        ASTNode* bin = ast_new(NODE_BINARY, expr->line);
        bin->as.binary.op = is_inc ? TOKEN_PLUS : TOKEN_MINUS;
        bin->as.binary.left = var_ref;
        bin->as.binary.right = one;
        ASTNode* n = ast_new(NODE_ASSIGN, expr->line);
        n->as.assign.name = (char*)malloc(strlen(expr->as.identifier.name) + 1);
        strcpy(n->as.assign.name, expr->as.identifier.name);
        ast_free(expr);
        n->as.assign.value = bin;
        consume(p, TOKEN_SEMICOLON, "Expected ';' after i++ / i--.");
        return n;
    }

    /* Compound assignment: += -= *= /= %= */
    if (expr->type == NODE_IDENTIFIER &&
        (check(p, TOKEN_PLUS_EQUAL) || check(p, TOKEN_MINUS_EQUAL) ||
         check(p, TOKEN_STAR_EQUAL) || check(p, TOKEN_SLASH_EQUAL) ||
         check(p, TOKEN_PERCENT_EQUAL))) {
        TokenType compound = peek_tok(p)->type;
        advance_tok(p);
        TokenType op;
        switch (compound) {
        case TOKEN_PLUS_EQUAL:    op = TOKEN_PLUS; break;
        case TOKEN_MINUS_EQUAL:   op = TOKEN_MINUS; break;
        case TOKEN_STAR_EQUAL:    op = TOKEN_STAR; break;
        case TOKEN_SLASH_EQUAL:   op = TOKEN_SLASH; break;
        case TOKEN_PERCENT_EQUAL: op = TOKEN_PERCENT; break;
        default:                  op = TOKEN_PLUS; break;
        }
        /* Desugar: i op= rhs → i = i op rhs */
        ASTNode* var_ref = ast_new(NODE_IDENTIFIER, expr->line);
        var_ref->as.identifier.name = copy_lexeme_str(expr->as.identifier.name, (int)strlen(expr->as.identifier.name));
        var_ref->as.identifier.length = (int)strlen(expr->as.identifier.name);
        ASTNode* rhs = expression(p);
        ASTNode* bin = ast_new(NODE_BINARY, expr->line);
        bin->as.binary.op = op;
        bin->as.binary.left = var_ref;
        bin->as.binary.right = rhs;
        ASTNode* n = ast_new(NODE_ASSIGN, expr->line);
        n->as.assign.name = (char*)malloc(strlen(expr->as.identifier.name) + 1);
        strcpy(n->as.assign.name, expr->as.identifier.name);
        ast_free(expr);
        n->as.assign.value = bin;
        consume(p, TOKEN_SEMICOLON, "Expected ';' after compound assignment.");
        return n;
    }

    /* Check for assignment: ident = expr */
    if (match(p, TOKEN_EQUAL)) {
        if (expr->type == NODE_IDENTIFIER) {
            ASTNode* n = ast_new(NODE_ASSIGN, expr->line);
            n->as.assign.name = (char*)malloc(strlen(expr->as.identifier.name) + 1);
            strcpy(n->as.assign.name, expr->as.identifier.name);
            ast_free(expr);
            n->as.assign.value = expression(p);
            consume(p, TOKEN_SEMICOLON, "Expected ';' after assignment.");
            return n;
        } else if (expr->type == NODE_INDEX) {
            ASTNode* n = ast_new(NODE_INDEX_ASSIGN, expr->line);
            n->as.index_assign.object = expr->as.index_access.object;
            n->as.index_assign.index = expr->as.index_access.index;
            n->as.index_assign.value = expression(p);
            free(expr);
            consume(p, TOKEN_SEMICOLON, "Expected ';'.");
            return n;
        } else if (expr->type == NODE_UNARY && expr->as.unary.op == TOKEN_STAR) {
            ASTNode* n = ast_new(NODE_INDEX_ASSIGN, expr->line);
            n->as.index_assign.object = expr->as.unary.operand;
            n->as.index_assign.index = nullptr;
            n->as.index_assign.value = expression(p);
            free(expr);
            consume(p, TOKEN_SEMICOLON, "Expected ';'.");
            return n;
        }
    }
    ASTNode* s = ast_new(NODE_EXPR_STMT, expr->line);
    s->as.child = expr;
    consume(p, TOKEN_SEMICOLON, "Expected ';' after expression.");
    return s;
}

static ASTNode* declaration(Parser* p) {
    /* use filename.42AHH; */
    if (match(p, TOKEN_USE)) {
        int line = previous(p)->line;
        /* Read all tokens until semicolon and concatenate to form filename */
        const char* start = peek_tok(p)->start;
        const char* end = start;
        while (!check(p, TOKEN_SEMICOLON) && !is_at_end(p)) {
            Token* t = advance_tok(p);
            end = t->start + t->length;
        }
        int len = (int)(end - start);
        ASTNode* n = ast_new(NODE_USE, line);
        n->as.use_file = (char*)malloc(len + 1);
        memcpy(n->as.use_file, start, len);
        n->as.use_file[len] = '\0';
        consume(p, TOKEN_SEMICOLON, "Expected ';' after use statement.");
        return n;
    }
    if (match(p, TOKEN_TANTRUM)) return func_declaration(p);
    /* Typed variable declaration: int x = 5; */
    if (is_type_token(peek_tok(p)->type) && p->current + 1 < p->tokens->count &&
        p->tokens->tokens[p->current + 1].type == TOKEN_IDENTIFIER) {
        Token* type = advance_tok(p);
        Token* name = advance_tok(p);
        ASTNode* n = ast_new(NODE_VAR_DECL, type->line);
        n->as.var_decl.type_name = copy_lexeme(type);
        n->as.var_decl.name = copy_lexeme(name);
        n->as.var_decl.init = nullptr;
        if (match(p, TOKEN_EQUAL)) {
            n->as.var_decl.init = expression(p);
        }
        consume(p, TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
        return n;
    }
    return statement(p);
}

ASTNode* parser_parse(TokenList* tokens) {
    Parser p;
    p.tokens = tokens;
    p.current = 0;
    p.had_error = false;

    ASTNode* program = ast_new(NODE_PROGRAM, 1);
    nodelist_init(&program->as.program);

    while (!is_at_end(&p)) {
        nodelist_add(&program->as.program, declaration(&p));
        if (p.had_error) break;
    }
    return program;
}
