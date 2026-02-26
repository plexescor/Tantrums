#include "builtins.h"
#include <cstdio>
#include <cstring>

/* print(x) — prints value followed by newline */
static Value native_print(VM* vm, int argc, Value* args) {
    (void)vm;
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        value_print(args[i]);
    }
    printf("\n");
    return NULL_VAL;
}

/* input(prompt) — prints prompt, reads line, returns string */
static Value native_input(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc >= 1 && IS_STRING(args[0])) {
        printf("%s", AS_CSTRING(args[0]));
    }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL_VAL;
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return OBJ_VAL(obj_string_new(buf, len));
}

/* len(x) — returns length of string, list, or map */
static Value native_len(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1) return INT_VAL(0);
    Value v = args[0];
    if (IS_STRING(v)) return INT_VAL(AS_STRING(v)->length);
    if (IS_LIST(v))   return INT_VAL(AS_LIST(v)->count);
    if (IS_MAP(v))    return INT_VAL(AS_MAP(v)->count);
    return INT_VAL(0);
}

/* range(n) — returns list [0, 1, ..., n-1] */
static Value native_range(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return OBJ_VAL(obj_list_new());
    int64_t n = AS_INT(args[0]);
    ObjList* list = obj_list_new();
    for (int64_t i = 0; i < n; i++) {
        obj_list_append(list, INT_VAL(i));
    }
    return OBJ_VAL(list);
}

/* type(x) — returns type name as string */
static Value native_type(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1) return OBJ_VAL(obj_string_new("null", 4));
    const char* name = value_type_name(args[0]);
    return OBJ_VAL(obj_string_new(name, (int)strlen(name)));
}

/* append(list, value) — appends value to list */
static Value native_append(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc >= 2 && IS_LIST(args[0])) {
        obj_list_append(AS_LIST(args[0]), args[1]);
    }
    return NULL_VAL;
}

static void define_native(VM* vm, const char* name, NativeFn fn);

void builtins_register(VM* vm) {
    define_native(vm, "print",  native_print);
    define_native(vm, "input",  native_input);
    define_native(vm, "len",    native_len);
    define_native(vm, "range",  native_range);
    define_native(vm, "type",   native_type);
    define_native(vm, "append", native_append);
}

static void define_native(VM* vm, const char* name, NativeFn fn) {
    ObjString* s = obj_string_new(name, (int)strlen(name));
    ObjNative* native = obj_native_new(fn, name);
    table_set(&vm->globals, s, OBJ_VAL(native));
}
