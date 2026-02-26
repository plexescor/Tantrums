#ifndef TANTRUMS_MEMORY_H
#define TANTRUMS_MEMORY_H

#include "value.h"

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size);
void  tantrums_free_all_objects(void);

#endif
