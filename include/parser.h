#ifndef TANTRUMS_PARSER_H
#define TANTRUMS_PARSER_H

#include "ast.h"
#include "token.h"

ASTNode* parser_parse(TokenList* tokens);

#endif
