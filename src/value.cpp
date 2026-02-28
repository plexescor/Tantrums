#include "value.h"
#include "chunk.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

Obj* all_objects = nullptr;

static Obj* allocate_obj(size_t size, ObjType type) {
    Obj* obj = (Obj*)tantrums_realloc(nullptr, 0, size);
    obj->type = type;
    obj->refcount = 1;
    obj->is_manual = false;
    obj->is_marked = false;
    obj->next = all_objects;
    all_objects = obj;
    return obj;
}

uint32_t hash_string(const char* key, int length) {
    /* Murmur3-inspired: much better avalanche than plain FNV for sequential keys like "key0", "key1" */
    uint32_t h = 0x9747b28cu ^ (uint32_t)length;
    const uint8_t* data = (const uint8_t*)key;
    int i = 0;
    /* Process 4 bytes at a time */
    for (; i + 4 <= length; i += 4) {
        uint32_t k;
        memcpy(&k, data + i, 4);
        k *= 0xcc9e2d51u;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593u;
        h ^= k;
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64u;
    }
    /* Remaining bytes */
    uint32_t tail = 0;
    switch (length - i) {
        case 3: tail ^= (uint32_t)data[i+2] << 16; /* fall through */
        case 2: tail ^= (uint32_t)data[i+1] << 8;  /* fall through */
        case 1: tail ^= (uint32_t)data[i];
                tail *= 0xcc9e2d51u;
                tail = (tail << 15) | (tail >> 17);
                tail *= 0x1b873593u;
                h ^= tail;
    }
    /* Finalization mix — force all bits to avalanche */
    h ^= (uint32_t)length;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h ? h : 1; /* never return 0 — reserved for empty slots */
}

ObjString* obj_string_new(const char* chars, int length) {
    uint32_t h = hash_string(chars, length);
    
    /* Create new string */
    ObjString* s = (ObjString*)allocate_obj(sizeof(ObjString), OBJ_STRING);
    s->obj.is_manual = true;
    s->length = length;
    s->capacity = length;
    s->is_mutable = false;
    
    // Allocate memory through gc-tracked realloc
    s->chars = (char*)tantrums_realloc(nullptr, 0, length + 1);
    memcpy(s->chars, chars, length);
    s->chars[length] = '\0';
    s->hash = h;
    s->obj.is_manual = false;
    
    return s;
}

ObjString* obj_string_clone_mutable(ObjString* a) {
    ObjString* r = obj_string_new(a->chars, a->length);
    r->is_mutable = true;
    return r;
}

void obj_string_append(ObjString* a, const char* chars, int length) {
    if (a->length + length > a->capacity) {
        int old_cap = a->capacity;
        int new_cap = old_cap < 8 ? 8 : old_cap * 2;
        while (new_cap < a->length + length) new_cap *= 2;
        a->chars = (char*)tantrums_realloc(a->chars, old_cap + 1, new_cap + 1);
        a->capacity = new_cap;
    }
    memcpy(a->chars + a->length, chars, length);
    a->length += length;
    a->chars[a->length] = '\0';
    /* Recompute hash using the full string — consistent with hash_string() */
    a->hash = hash_string(a->chars, a->length);
}

ObjString* obj_string_concat(ObjString* a, ObjString* b) {
    /* Only mutate in-place if mutable AND exclusively owned (refcount == 1).
     * If refcount > 1 the string is aliased somewhere else — mutating it
     * would silently corrupt every other holder (this was the footer bug). */
    if (a->is_mutable && a->obj.refcount == 1) {
        obj_string_append(a, b->chars, b->length);
        return a;
    }
    ObjString* r = obj_string_clone_mutable(a);
    r->obj.is_manual = true;
    obj_string_append(r, b->chars, b->length);
    r->obj.is_manual = false;
    return r;
}
/* ── List ─────────────────────────────────────────── */
ObjList* obj_list_new(void) {
    ObjList* l = (ObjList*)allocate_obj(sizeof(ObjList), OBJ_LIST);
    l->items = nullptr; l->count = 0; l->capacity = 0;
    return l;
}

ObjList* obj_list_clone(ObjList* origin) {
    ObjList* l = obj_list_new();
    l->obj.is_manual = true;
    for (int i = 0; i < origin->count; i++) {
        obj_list_append(l, origin->items[i]);
    }
    l->obj.is_manual = false;
    return l;
}

void obj_list_append(ObjList* l, Value v) {
    if (l->count >= l->capacity) {
        int cap = l->capacity < 8 ? 8 : l->capacity * 2;
        l->items = (Value*)realloc(l->items, sizeof(Value) * cap);
        l->capacity = cap;
    }
    l->items[l->count++] = v;
    value_incref(v);
}

/* ── Map ──────────────────────────────────────────── */
ObjMap* obj_map_new(void) {
    ObjMap* m = (ObjMap*)allocate_obj(sizeof(ObjMap), OBJ_MAP);
    m->entries = nullptr; m->count = 0; m->capacity = 0;
    return m;
}

uint32_t value_hash(Value v) {
    switch (v.type) {
        case VAL_NULL:  return 1;
        case VAL_BOOL:  return AS_BOOL(v) ? 3 : 2;
        case VAL_INT:   {
            /* Murmur3 integer finalizer — eliminates clustering for sequential keys */
            uint64_t i = (uint64_t)AS_INT(v);
            uint32_t h = (uint32_t)(i ^ (i >> 32));
            h ^= h >> 16;
            h *= 0x45d9f3bu;
            h ^= h >> 16;
            return h ? h : 1;
        }
        case VAL_FLOAT: {
            double d = AS_FLOAT(v);
            uint64_t bits;
            memcpy(&bits, &d, sizeof(double));
            uint32_t h = (uint32_t)(bits ^ (bits >> 32));
            h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13;
            return h ? h : 1;
        }
        case VAL_OBJ: {
            if (IS_STRING(v)) return AS_STRING(v)->hash;
            return (uint32_t)((uintptr_t)AS_OBJ(v) >> 3);
        }
    }
    return 1;
}

static void map_grow(ObjMap* m) {
    int cap = m->capacity < 8 ? 8 : m->capacity * 2;
    MapEntry* old = m->entries;
    int old_cap = m->capacity;
    m->entries = (MapEntry*)calloc(cap, sizeof(MapEntry));
    m->capacity = cap;
    m->count = 0;
    for (int i = 0; i < old_cap; i++) {
        if (!old[i].occupied) continue;
        obj_map_set(m, old[i].key, old[i].value);
    }
    free(old);
}

bool obj_map_set(ObjMap* m, Value key, Value value) {
    if (m->count + 1 > m->capacity * 0.75) map_grow(m);
    uint32_t idx = value_hash(key) & (m->capacity - 1);
    for (;;) {
        MapEntry* e = &m->entries[idx];
        if (!e->occupied) {
            e->key = key; e->value = value; e->occupied = true;
            m->count++;
            return true;
        }
        if (value_equal(e->key, key)) {
            e->value = value;
            return false;
        }
        idx = (idx + 1) & (m->capacity - 1);
    }
}

bool obj_map_get(ObjMap* m, Value key, Value* out) {
    if (m->count == 0) return false;
    uint32_t idx = value_hash(key) & (m->capacity - 1);
    for (;;) {
        MapEntry* e = &m->entries[idx];
        if (!e->occupied) return false;
        if (value_equal(e->key, key)) {
            *out = e->value; return true;
        }
        idx = (idx + 1) & (m->capacity - 1);
    }
}

/* ── Function / Native / Pointer ──────────────────── */
ObjFunction* obj_function_new(void) {
    ObjFunction* f = (ObjFunction*)allocate_obj(sizeof(ObjFunction), OBJ_FUNCTION);
    f->arity = 0;
    f->chunk = (Chunk*)calloc(1, sizeof(Chunk));
    chunk_init(f->chunk);
    f->name = nullptr;
    return f;
}

ObjNative* obj_native_new(NativeFn fn, const char* name) {
    ObjNative* n = (ObjNative*)allocate_obj(sizeof(ObjNative), OBJ_NATIVE);
    n->function = fn;
    n->name = name;
    return n;
}

ObjPointer* obj_pointer_new(Value* target) {
    ObjPointer* p = (ObjPointer*)allocate_obj(sizeof(ObjPointer), OBJ_POINTER);
    p->target = target;
    p->is_valid = true;
    p->alloc_size = 0;
    p->alloc_line = 0;
    p->alloc_type = nullptr;
    p->alloc_func = nullptr;
    p->scope_depth = 0;
    p->escaped = false;
    return p;
}

ObjRange* obj_range_new(int64_t start, int64_t end, int64_t step) {
    ObjRange* r = (ObjRange*)allocate_obj(sizeof(ObjRange), OBJ_RANGE);
    r->start = start;
    r->end = end;
    r->step = step;
    
    if (step > 0 && end > start) {
        r->length = (end - start + step - 1) / step;
    } else if (step < 0 && end < start) {
        r->length = (end - start + step + 1) / step;
    } else {
        r->length = 0;
    }
    
    return r;
}

/* ── Ref counting ─────────────────────────────────── */
void value_incref(Value v) {
    if (!IS_OBJ(v) || !AS_OBJ(v)) return;
    AS_OBJ(v)->refcount++;
}

void value_decref(Value v) {
    if (!IS_OBJ(v) || !AS_OBJ(v)) return;
    Obj* o = AS_OBJ(v);
    if (o->is_manual) return;
    o->refcount--;
    /* Don't free here — bulk cleanup at shutdown handles it.
       This avoids double-free when all_objects list is walked. */
}

void obj_free(Obj* obj) {
    switch (obj->type) {
    case OBJ_STRING: {
        ObjString* s = (ObjString*)obj;
        tantrums_realloc(s->chars, s->capacity + 1, 0);
        tantrums_realloc(obj, sizeof(ObjString), 0);
        break;
    }
    case OBJ_POINTER: {
        ObjPointer* p = (ObjPointer*)obj;
        if (p->is_valid) {
            tantrums_realloc(p->target, sizeof(Value), 0);
        }
        tantrums_realloc(obj, sizeof(ObjPointer), 0);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)obj;
        chunk_free(f->chunk);
        tantrums_realloc(f->chunk, sizeof(Chunk), 0);
        tantrums_realloc(obj, sizeof(ObjFunction), 0);
        break;
    }
    case OBJ_NATIVE: {
        tantrums_realloc(obj, sizeof(ObjNative), 0);
        break;
    }
    case OBJ_LIST: {
        ObjList* lst = (ObjList*)obj;
        for (int i = 0; i < lst->count; i++) {
             value_decref(lst->items[i]);
        }
        tantrums_realloc(lst->items, sizeof(Value) * lst->capacity, 0);
        tantrums_realloc(obj, sizeof(ObjList), 0);
        break;
    }
    case OBJ_MAP: {
        ObjMap* map = (ObjMap*)obj;
        for (int i = 0; i < map->capacity; i++) {
             MapEntry* e = &map->entries[i];
             if (!IS_NULL(e->key) || !IS_NULL(e->value)) {
                 value_decref(e->key);
                 value_decref(e->value);
             }
        }
        tantrums_realloc(map->entries, sizeof(MapEntry) * map->capacity, 0);
        tantrums_realloc(obj, sizeof(ObjMap), 0);
        break;
    }
    case OBJ_RANGE: {
        tantrums_realloc(obj, sizeof(ObjRange), 0);
        break;
    }
    }
}

/* ── Utilities ────────────────────────────────────── */
double value_as_number(Value v) {
    if (IS_INT(v)) return (double)AS_INT(v);
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    return 0.0;
}

void value_print(Value v) {
    switch (v.type) {
    case VAL_INT:   printf("%lld", (long long)AS_INT(v)); break;
    case VAL_FLOAT: {
        double d = AS_FLOAT(v);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.10f", d);
        /* Strip trailing zeros after decimal point */
        char* dot = strchr(buf, '.');
        if (dot) {
            char* end = buf + strlen(buf) - 1;
            while (end > dot + 1 && *end == '0') end--;
            *(end + 1) = '\0';
        }
        printf("%s", buf);
    } break;
    case VAL_BOOL:  printf(AS_BOOL(v) ? "true" : "false"); break;
    case VAL_NULL:  printf("null"); break;
    case VAL_OBJ:
        switch (AS_OBJ(v)->type) {
        case OBJ_STRING:   printf("%s", AS_CSTRING(v)); break;
        case OBJ_LIST:     {
            ObjList* l = AS_LIST(v);
            printf("[");
            for (int i = 0; i < l->count; i++) {
                if (i > 0) printf(", ");
                value_print(l->items[i]);
            }
            printf("]");
        } break;
        case OBJ_MAP:      printf("<map>"); break;
        case OBJ_FUNCTION: printf("<fn %s>", AS_FUNCTION(v)->name ? AS_FUNCTION(v)->name->chars : "script"); break;
        case OBJ_NATIVE:   printf("<native %s>", AS_NATIVE(v)->name); break;
        case OBJ_POINTER:  printf("<ptr>"); break;
        case OBJ_RANGE: {
            ObjRange* r = AS_RANGE(v);
            printf("[");
            for (int64_t i = 0; i < r->length; i++) {
                if (i > 0) printf(", ");
                printf("%lld", (long long)(r->start + i * r->step));
            }
            printf("]");
        } break;
        }
        break;
    }
}

bool value_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case VAL_INT:   return AS_INT(a) == AS_INT(b);
    case VAL_FLOAT: return AS_FLOAT(a) == AS_FLOAT(b);
    case VAL_BOOL:  return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NULL:  return true;
    case VAL_OBJ:   {
        if (AS_OBJ(a) == AS_OBJ(b)) return true;
        if (OBJ_TYPE(a) != OBJ_TYPE(b)) return false;
        if (IS_STRING(a)) {
            ObjString* sa = AS_STRING(a);
            ObjString* sb = AS_STRING(b);
            if (sa->length != sb->length) return false;
            return memcmp(sa->chars, sb->chars, sa->length) == 0;
        }
        return false;
    }
    }
    return false;
}

const char* value_type_name(Value v) {
    switch (v.type) {
    case VAL_INT:   return "int";
    case VAL_FLOAT: return "float";
    case VAL_BOOL:  return "bool";
    case VAL_NULL:  return "null";
    case VAL_OBJ:
        switch (AS_OBJ(v)->type) {
        case OBJ_STRING:   return "string";
        case OBJ_LIST:     return "list";
        case OBJ_MAP:      return "map";
        case OBJ_FUNCTION: return "function";
        case OBJ_NATIVE:   return "native";
        case OBJ_POINTER:  return "pointer";
        case OBJ_RANGE:    return "range";
        }
    }
    return "unknown";
}