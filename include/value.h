#ifndef TANTRUMS_VALUE_H
#define TANTRUMS_VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjList ObjList;
typedef struct ObjMap ObjMap;
typedef struct ObjFunction ObjFunction;
typedef struct ObjNative ObjNative;
typedef struct ObjPointer ObjPointer;
typedef struct ObjRange ObjRange;
typedef struct Chunk Chunk;
typedef struct VM VM;

typedef enum { VAL_INT, VAL_FLOAT, VAL_BOOL, VAL_NULL, VAL_OBJ } ValueType;

typedef struct {
    ValueType type;
    union { int64_t integer; double floating; bool boolean; Obj* obj; } as;
} Value;

/* C++ compatible value constructors */
static inline Value INT_VAL(int64_t v)  { Value r; r.type = VAL_INT;   r.as.integer  = v; return r; }
static inline Value FLOAT_VAL(double v) { Value r; r.type = VAL_FLOAT; r.as.floating = v; return r; }
static inline Value BOOL_VAL(bool v)    { Value r; r.type = VAL_BOOL;  r.as.boolean  = v; return r; }
static inline Value OBJ_VAL(void* o)    { Value r; r.type = VAL_OBJ;   r.as.obj = (Obj*)o; return r; }
static inline Value NULL_VAL_MAKE()     { Value r; r.type = VAL_NULL;  r.as.integer  = 0; return r; }
#define NULL_VAL NULL_VAL_MAKE()

typedef enum { OBJ_STRING, OBJ_LIST, OBJ_MAP, OBJ_FUNCTION, OBJ_NATIVE, OBJ_POINTER, OBJ_RANGE } ObjType;

struct Obj        { ObjType type; int refcount; bool is_manual; bool is_marked; Obj* next; };
struct ObjString  { Obj obj; int length; int capacity; bool is_mutable; char* chars; uint32_t hash; };
struct ObjList    { Obj obj; Value* items; int count; int capacity; bool escaped; int scope_depth; bool auto_manage; };

typedef struct { Value key; Value value; bool occupied; } MapEntry;
struct ObjMap     { Obj obj; MapEntry* entries; int count; int capacity; bool escaped; int scope_depth; bool auto_manage; };

typedef Value (*NativeFn)(VM* vm, int arg_count, Value* args);
struct ObjNative  { Obj obj; NativeFn function; const char* name; };
struct ObjFunction{ Obj obj; int arity; Chunk* chunk; ObjString* name; };
struct ObjPointer { Obj obj; Value* target; bool is_valid; size_t alloc_size; int alloc_line; ObjString* alloc_type; ObjString* alloc_func; int scope_depth; bool escaped; bool auto_manage; };
struct ObjRange { Obj obj; int64_t start; int64_t end; int64_t step; int64_t length; };

#define IS_INT(v)      ((v).type == VAL_INT)
#define IS_FLOAT(v)    ((v).type == VAL_FLOAT)
#define IS_BOOL(v)     ((v).type == VAL_BOOL)
#define IS_NULL(v)     ((v).type == VAL_NULL)
#define IS_OBJ(v)      ((v).type == VAL_OBJ)
#define OBJ_TYPE(v)    (AS_OBJ(v)->type)
#define IS_STRING(v)   (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_STRING)
#define IS_LIST(v)     (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_LIST)
#define IS_MAP(v)      (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_MAP)
#define IS_FUNCTION(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_FUNCTION)
#define IS_NATIVE(v)   (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_NATIVE)
#define IS_POINTER(v)  (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_POINTER)
#define IS_RANGE(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_RANGE)

#define AS_INT(v)      ((v).as.integer)
#define AS_FLOAT(v)    ((v).as.floating)
#define AS_BOOL(v)     ((v).as.boolean)
#define AS_OBJ(v)      ((v).as.obj)
#define AS_STRING(v)   ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)  (((ObjString*)AS_OBJ(v))->chars)
#define AS_LIST(v)     ((ObjList*)AS_OBJ(v))
#define AS_MAP(v)      ((ObjMap*)AS_OBJ(v))
#define AS_FUNCTION(v) ((ObjFunction*)AS_OBJ(v))
#define AS_NATIVE(v)   ((ObjNative*)AS_OBJ(v))
#define AS_POINTER(v)  ((ObjPointer*)AS_OBJ(v))
#define AS_RANGE(v)    ((ObjRange*)AS_OBJ(v))

double       value_as_number(Value v);
ObjString*   obj_string_new(const char* chars, int length);
ObjString*   obj_string_clone_mutable(ObjString* a);
void         obj_string_append(ObjString* a, const char* chars, int length);
ObjString*   obj_string_concat(ObjString* a, ObjString* b);
ObjList*     obj_list_new(void);
ObjList*     obj_list_clone(ObjList* origin);
void         obj_list_append(ObjList* list, Value value);
ObjMap*      obj_map_new(void);
bool         obj_map_set(ObjMap* map, Value key, Value value);
bool         obj_map_get(ObjMap* map, Value key, Value* out);
ObjFunction* obj_function_new(void);
ObjNative*   obj_native_new(NativeFn fn, const char* name);
ObjPointer*  obj_pointer_new(Value* target);
ObjRange*    obj_range_new(int64_t start, int64_t end, int64_t step);
void         value_incref(Value v);
void         value_decref(Value v);
void         obj_free(Obj* obj);
void         value_print(Value v);
bool         value_equal(Value a, Value b);
const char*  value_type_name(Value v);
uint32_t     hash_string(const char* key, int length);
uint32_t     value_hash(Value v);

extern Obj* all_objects;

#endif