#ifndef TANTRUMS_CHUNK_H
#define TANTRUMS_CHUNK_H

#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_NULL, OP_TRUE, OP_FALSE,

    /* Arithmetic */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEGATE,

    /* Comparison */
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTE, OP_GTE,

    /* Logical */
    OP_NOT,

    /* Variables */
    OP_GET_LOCAL, OP_SET_LOCAL,
    OP_GET_GLOBAL, OP_SET_GLOBAL, OP_DEFINE_GLOBAL,

    /* Control flow */
    OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP,
    OP_POP,

    /* Functions */
    OP_CALL, OP_RETURN,

    /* Collections */
    OP_LIST_NEW,   /* operand = item count */
    OP_MAP_NEW,    /* operand = pair count */
    OP_INDEX_GET,
    OP_INDEX_SET,

    /* Built-ins */
    OP_PRINT, OP_LEN,

    /* Memory / pointers */
    OP_ALLOC, OP_FREE,
    OP_PTR_REF, OP_PTR_DEREF, OP_PTR_SET,

    /* Type casting */
    OP_CAST,        /* operand: 0=int, 1=float, 2=string, 3=bool */

    /* Error */
    OP_THROW,

    OP_HALT,
} OpCode;

struct Chunk {
    uint8_t* code;
    int      count;
    int      capacity;
    int*     lines;
    Value*   constants;
    int      const_count;
    int      const_capacity;
};

void chunk_init(Chunk* chunk);
void chunk_free(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, int line);
int  chunk_add_constant(Chunk* chunk, Value value);

#endif
