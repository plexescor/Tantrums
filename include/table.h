#ifndef TANTRUMS_TABLE_H
#define TANTRUMS_TABLE_H

#include "value.h"

typedef struct {
    ObjString* key;
    Value      value;
} TableEntry;

typedef struct {
    TableEntry* entries;
    int         count;
    int         capacity;
} Table;

void       table_init(Table* table);
void       table_free(Table* table);
bool       table_set(Table* table, ObjString* key, Value value);
bool       table_get(Table* table, ObjString* key, Value* out);
bool       table_delete(Table* table, ObjString* key);
ObjString* table_find_string(Table* table, const char* chars, int length, uint32_t hash);

#endif
