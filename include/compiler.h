#ifndef TANTRUMS_COMPILER_H
#define TANTRUMS_COMPILER_H

#include "ast.h"
#include "chunk.h"

typedef enum {
    MODE_BOTH = 0,      /* default: typed + dynamic coexist */
    MODE_STATIC = 1,    /* all vars must have type annotations */
    MODE_DYNAMIC = 2,   /* no type checking at all */
} CompileMode;

ObjFunction* compiler_compile(ASTNode* program, CompileMode mode);

#endif
