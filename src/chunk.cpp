#include "chunk.h"
#include <cstdlib>

void chunk_init(Chunk* c) {
    c->code = nullptr; c->count = 0; c->capacity = 0;
    c->lines = nullptr;
    c->constants = nullptr; c->const_count = 0; c->const_capacity = 0;
}

void chunk_write(Chunk* c, uint8_t byte, int line) {
    if (c->count >= c->capacity) {
        int cap = c->capacity < 8 ? 8 : c->capacity * 2;
        c->code  = (uint8_t*)realloc(c->code, cap);
        c->lines = (int*)realloc(c->lines, sizeof(int) * cap);
        c->capacity = cap;
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    c->count++;
}

int chunk_add_constant(Chunk* c, Value value) {
    if (c->const_count >= c->const_capacity) {
        int cap = c->const_capacity < 8 ? 8 : c->const_capacity * 2;
        c->constants = (Value*)realloc(c->constants, sizeof(Value) * cap);
        c->const_capacity = cap;
    }
    c->constants[c->const_count] = value;
    return c->const_count++;
}

void chunk_free(Chunk* c) {
    free(c->code);
    free(c->lines);
    free(c->constants);
    chunk_init(c);
}
