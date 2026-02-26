#ifndef TANTRUMS_MEMORY_H
#define TANTRUMS_MEMORY_H

#include "value.h"

extern size_t tantrums_bytes_allocated;
extern size_t tantrums_next_gc;

#define DEBUG_LOG_GC 0

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size);
void  tantrums_free_all_objects(void);
void  tantrums_gc_collect(void);

#endif
