#include "table.h"
#include <cstdlib>
#include <cstring>

#define TABLE_MAX_LOAD 0.75

void table_init(Table* t) { t->entries = nullptr; t->count = 0; t->capacity = 0; }

void table_free(Table* t) { free(t->entries); table_init(t); }

static TableEntry* find_entry(TableEntry* entries, int cap, ObjString* key) {
    uint32_t idx = key->hash & (cap - 1);
    TableEntry* tombstone = nullptr;
    for (;;) {
        TableEntry* e = &entries[idx];
        if (e->key == nullptr) {
            if (IS_NULL(e->value)) return tombstone ? tombstone : e;
            if (!tombstone) tombstone = e;
        } else if (e->key == key) {
            return e;
        }
        idx = (idx + 1) & (cap - 1);
    }
}

static void adjust_capacity(Table* t, int cap) {
    TableEntry* entries = (TableEntry*)calloc(cap, sizeof(TableEntry));
    for (int i = 0; i < cap; i++) { entries[i].key = nullptr; entries[i].value = NULL_VAL; }
    t->count = 0;
    for (int i = 0; i < t->capacity; i++) {
        TableEntry* src = &t->entries[i];
        if (!src->key) continue;
        TableEntry* dst = find_entry(entries, cap, src->key);
        dst->key = src->key;
        dst->value = src->value;
        t->count++;
    }
    free(t->entries);
    t->entries = entries;
    t->capacity = cap;
}

bool table_set(Table* t, ObjString* key, Value value) {
    if (t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
        int cap = t->capacity < 8 ? 8 : t->capacity * 2;
        adjust_capacity(t, cap);
    }
    TableEntry* e = find_entry(t->entries, t->capacity, key);
    bool is_new = (e->key == nullptr);
    if (is_new && IS_NULL(e->value)) t->count++;
    e->key = key;
    e->value = value;
    return is_new;
}

bool table_get(Table* t, ObjString* key, Value* out) {
    if (t->count == 0) return false;
    TableEntry* e = find_entry(t->entries, t->capacity, key);
    if (!e->key) return false;
    *out = e->value;
    return true;
}

bool table_delete(Table* t, ObjString* key) {
    if (t->count == 0) return false;
    TableEntry* e = find_entry(t->entries, t->capacity, key);
    if (!e->key) return false;
    e->key = nullptr;
    e->value = BOOL_VAL(true); /* tombstone */
    return true;
}

ObjString* table_find_string(Table* t, const char* chars, int length, uint32_t hash) {
    if (t->count == 0) return nullptr;
    uint32_t idx = hash & (t->capacity - 1);
    for (;;) {
        TableEntry* e = &t->entries[idx];
        if (!e->key) { if (IS_NULL(e->value)) return nullptr; }
        else if (e->key->length == length && e->key->hash == hash &&
                 memcmp(e->key->chars, chars, length) == 0)
            return e->key;
        idx = (idx + 1) & (t->capacity - 1);
    }
}
