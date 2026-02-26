#include "memory.h"
#include "value.h"
#include <cstdlib>

/* Defined in value.cpp */
extern ObjString** intern_table;
extern int intern_count;
extern int intern_cap;

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size) {
    (void)old_size;
    if (new_size == 0) { free(ptr); return nullptr; }
    return realloc(ptr, new_size);
}

void tantrums_free_all_objects(void) {
    Obj* obj = all_objects;
    while (obj) {
        Obj* next = obj->next;
        obj_free(obj);
        obj = next;
    }
    all_objects = nullptr;

    /* Clean up intern table */
    free(intern_table);
    intern_table = nullptr;
    intern_count = 0;
    intern_cap = 0;
}
