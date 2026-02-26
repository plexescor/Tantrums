#ifndef TANTRUMS_COMPILER_H
#define TANTRUMS_COMPILER_H

#include "ast.h"
#include "chunk.h"

ObjFunction* compiler_compile(ASTNode* program);

#endif
