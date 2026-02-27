#include "value.h"
#include "chunk.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

Obj* all_objects = nullptr;

static Obj* allocate_obj(size_t size, ObjType type) {
    Obj* obj = (Obj*)malloc(size);
    obj->type = type;
    obj->refcount = 1;
    obj->is_manual = false;
    obj->is_marked = false;
    obj->next = all_objects;
    all_objects = obj;
    return obj;
}

uint32_t hash_string(const char* key, int length) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < length; i++) {
        h ^= (uint8_t)key[i];
        h *= 16777619u;
    }
    return h;
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
    
    /* Incrementally calculate FNV-1a hash */
    for (int i = 0; i < length; i++) {
        a->hash ^= (uint8_t)chars[i];
        a->hash *= 16777619u;
    }
    
    a->length += length;
    a->chars[a->length] = '\0';
}

ObjString* obj_string_concat(ObjString* a, ObjString* b) {
    if (a->is_mutable) {
        obj_string_append(a, b->chars, b->length);
        a->obj.refcount++; // Concatenation on stack gives another reference to a
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

bool obj_map_set(ObjMap* m, ObjString* key, Value value) {
    if (m->count + 1 > m->capacity * 0.75) map_grow(m);
    uint32_t idx = key->hash & (m->capacity - 1);
    for (;;) {
        MapEntry* e = &m->entries[idx];
        if (!e->occupied) {
            e->key = key; e->value = value; e->occupied = true;
            m->count++;
            return true;
        }
        if (e->key->hash == key->hash && e->key->length == key->length &&
            memcmp(e->key->chars, key->chars, key->length) == 0) {
            e->value = value;
            return false;
        }
        idx = (idx + 1) & (m->capacity - 1);
    }
}

bool obj_map_get(ObjMap* m, ObjString* key, Value* out) {
    if (m->count == 0) return false;
    uint32_t idx = key->hash & (m->capacity - 1);
    for (;;) {
        MapEntry* e = &m->entries[idx];
        if (!e->occupied) return false;
        if (e->key->hash == key->hash && e->key->length == key->length &&
            memcmp(e->key->chars, key->chars, key->length) == 0) {
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
    return p;
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
    case OBJ_STRING:  { ObjString* s = (ObjString*)obj; tantrums_realloc(s->chars, s->capacity + 1, 0); } break;
    case OBJ_LIST:    { ObjList* l = (ObjList*)obj;
                        free(l->items); } break;
    case OBJ_MAP:     { ObjMap* m = (ObjMap*)obj; free(m->entries); } break;
    case OBJ_FUNCTION:{ ObjFunction* f = (ObjFunction*)obj;
                        chunk_free(f->chunk); free(f->chunk); } break;
    case OBJ_NATIVE:  break;
    case OBJ_POINTER: break;
    }
    free(obj);
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
    case VAL_FLOAT: printf("%g", AS_FLOAT(v)); break;
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
        }
    }
    return "unknown";
}
