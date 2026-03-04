/*  runtime.cpp  —  Tantrums native runtime (linked with LLVM-generated code)
 *
 *  All functions use NaN-boxed uint64_t (TantrumsValue) at the ABI boundary.
 *  Internally we convert to/from the existing Value / Obj* types.
 */
#include "runtime.h"
#include "value.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <csetjmp>
#include <chrono>
#include <cinttypes>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

/* ══════════════════════════════════════════════════════════════════
 *  Internal helpers — convert between NaN-boxed TV and Value
 * ══════════════════════════════════════════════════════════════════ */

static Value tv_to_value(TantrumsValue tv) {
    switch (tv_tag(tv)) {
    case TV_TAG_INT:   return INT_VAL(tv_to_int(tv));
    case TV_TAG_FLOAT: return FLOAT_VAL(tv_to_float(tv));
    case TV_TAG_BOOL:  return BOOL_VAL(tv_to_bool(tv));
    case TV_TAG_NULL:  return NULL_VAL;
    case TV_TAG_OBJ:   return OBJ_VAL(tv_to_obj(tv));
    default:           return NULL_VAL;
    }
}

static TantrumsValue value_to_tv(Value v) {
    switch (v.type) {
    case VAL_INT:   return tv_int(v.as.integer);
    case VAL_FLOAT: return tv_float(v.as.floating);
    case VAL_BOOL:  return tv_bool(v.as.boolean);
    case VAL_NULL:  return TV_NULL;
    case VAL_OBJ:   return tv_obj(v.as.obj);
    default:        return TV_NULL;
    }
}

static double tv_as_number(TantrumsValue v, bool* ok) {
    int tag = tv_tag(v);
    if (tag == TV_TAG_INT)   { *ok = true; return (double)tv_to_int(v); }
    if (tag == TV_TAG_FLOAT) { *ok = true; return tv_to_float(v); }
    *ok = false;
    return 0.0;
}

/* Portable value-to-string: no stdout redirection needed.
 * Writes a string representation of a Value into buf. */
static void value_sprint(Value v, char* buf, size_t buf_size) {
    switch (v.type) {
    case VAL_INT:   snprintf(buf, buf_size, "%" PRId64, v.as.integer); break;
    case VAL_FLOAT: {
        snprintf(buf, buf_size, "%g", v.as.floating);
        break;
    }
    case VAL_BOOL:  snprintf(buf, buf_size, "%s", v.as.boolean ? "true" : "false"); break;
    case VAL_NULL:  snprintf(buf, buf_size, "null"); break;
    case VAL_OBJ: {
        if (!v.as.obj) { snprintf(buf, buf_size, "null"); break; }
        switch (v.as.obj->type) {
        case OBJ_STRING:
            snprintf(buf, buf_size, "%s", ((ObjString*)v.as.obj)->chars);
            break;
        case OBJ_LIST: {
            ObjList* list = (ObjList*)v.as.obj;
            int off = snprintf(buf, buf_size, "[");
            for (int i = 0; i < list->count && (size_t)off < buf_size - 2; i++) {
                if (i > 0) off += snprintf(buf + off, buf_size - off, ", ");
                char tmp[256];
                value_sprint(list->items[i], tmp, sizeof(tmp));
                off += snprintf(buf + off, buf_size - off, "%s", tmp);
            }
            snprintf(buf + off, buf_size - off, "]");
            break;
        }
        case OBJ_MAP:
            snprintf(buf, buf_size, "<map>");
            break;
        case OBJ_RANGE: {
            ObjRange* r = (ObjRange*)v.as.obj;
            snprintf(buf, buf_size, "range(%" PRId64 ", %" PRId64 ", %" PRId64 ")",
                     r->start, r->start + r->length * r->step, r->step);
            break;
        }
        case OBJ_POINTER: {
            ObjPointer* p = (ObjPointer*)v.as.obj;
            if (p->is_valid && p->target) {
                char inner[256];
                value_sprint(*p->target, inner, sizeof(inner));
                snprintf(buf, buf_size, "ptr(%s)", inner);
            } else {
                snprintf(buf, buf_size, "ptr(null)");
            }
            break;
        }
        default:
            snprintf(buf, buf_size, "<obj>");
            break;
        }
        break;
    }
    default: snprintf(buf, buf_size, "?"); break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Error handling — setjmp / longjmp exception system
 * ══════════════════════════════════════════════════════════════════ */

#define MAX_TRY_DEPTH 64

static jmp_buf   try_stack[MAX_TRY_DEPTH];
static int       try_depth = 0;
static TantrumsValue caught_exception = TV_NULL;

/* Stack trace for runtime errors */
#define MAX_CALL_STACK 256
typedef struct {
    const char* func_name;
    int line;
} CallRecord;

static CallRecord call_stack[MAX_CALL_STACK];
static int call_stack_depth = 0;

static void rt_fatal_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[Tantrums Runtime Error] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    /* Print stack trace */
    for (int i = call_stack_depth - 1; i >= 0; i--) {
        fprintf(stderr, "  [line %d] in %s\n", call_stack[i].line,
                call_stack[i].func_name ? call_stack[i].func_name : "<script>");
    }
    exit(1);
}

/* ══════════════════════════════════════════════════════════════════
 *  Scope tracking (Memory Layer 2)
 * ══════════════════════════════════════════════════════════════════ */

#define MAX_SCOPES 1024

static int    scope_depth = 0;
static Obj*   scope_markers[MAX_SCOPES];  /* all_objects head at scope entry */

/* Auto-free tracking */
typedef struct {
    const char* func_name;
    const char* type_name;
    int line;
    size_t size;
    int count;
} AutoFreeRecord;

static AutoFreeRecord* auto_free_records = nullptr;
static int auto_free_count = 0;
static int auto_free_capacity = 0;
static int total_auto_frees = 0;

bool global_autofree = true;
bool global_allow_leaks = false;
const char* current_bytecode_path = nullptr;

/* ══════════════════════════════════════════════════════════════════
 *  Runtime function implementations
 * ══════════════════════════════════════════════════════════════════ */

extern "C" {

/* ── Lifecycle ──────────────────────────────────────── */

void rt_init(void) {
    scope_depth = 0;
    try_depth = 0;
    call_stack_depth = 0;
    caught_exception = TV_NULL;
    auto_free_records = nullptr;
    auto_free_count = 0;
    auto_free_capacity = 0;
    total_auto_frees = 0;
}

void rt_shutdown(void) {
    /* Auto-free report */
    if (total_auto_frees > 0) {
        if (total_auto_frees <= 20) {
            printf("\nTANTRUMS AUTO-FREE REPORT\n");
            printf("============================\n");
            printf("Auto-Frees: %d allocations\n", total_auto_frees);
            printf("============================\n");
            size_t total_bytes = 0;
            for (int i = 0; i < auto_free_count; i++) {
                printf("  alloc at line %d in %s -- %s (%zu bytes)",
                       auto_free_records[i].line,
                       auto_free_records[i].func_name ? auto_free_records[i].func_name : "<script>",
                       auto_free_records[i].type_name ? auto_free_records[i].type_name : "dynamic",
                       auto_free_records[i].size);
                if (auto_free_records[i].count > 1)
                    printf(" [x%d]", auto_free_records[i].count);
                printf("\n");
                total_bytes += auto_free_records[i].size * auto_free_records[i].count;
            }
            printf("============================\n");
            printf("SUMMARY\n");
            printf("  Total auto-freed: %zu bytes\n", total_bytes);
            printf("                  = %.2f KB\n", total_bytes / 1024.0);
            printf("============================\n");
        }
    }

    /* Leak detection — scan all_objects for un-freed allocations */
    int leak_count = 0;
    size_t leak_bytes = 0;
    for (Obj* obj = all_objects; obj != nullptr; obj = obj->next) {
        if (obj->type == OBJ_POINTER) {
            ObjPointer* p = (ObjPointer*)obj;
            if (p->is_valid) {
                leak_count++;
                leak_bytes += p->alloc_size;
            }
        }
    }

    if (leak_count > 0) {
        if (leak_count <= 5) {
            fprintf(stderr, "\n[Tantrums Warning] Memory leak detected: %d allocation(s) not freed.\n", leak_count);
            for (Obj* obj = all_objects; obj != nullptr; obj = obj->next) {
                if (obj->type == OBJ_POINTER) {
                    ObjPointer* p = (ObjPointer*)obj;
                    if (p->is_valid) {
                        fprintf(stderr, "  alloc at line %d in %s -- %s (%zu bytes)\n",
                                p->alloc_line,
                                p->alloc_func ? p->alloc_func->chars : "<script>",
                                p->alloc_type ? p->alloc_type->chars : "dynamic",
                                p->alloc_size);
                    }
                }
            }
        } else {
            fprintf(stderr, "\n[Tantrums Warning] Memory leak detected: %d allocation(s) not freed.\n", leak_count);
            fprintf(stderr, "See memleaklog.txt in the same directory as the executing bytecode.\n");
        }
    }

    /* Free all objects */
    tantrums_free_all_objects();
    if (auto_free_records) { free(auto_free_records); auto_free_records = nullptr; }
}

/* ── Output ─────────────────────────────────────────── */

void rt_print(TantrumsValue* args, int32_t count) {
    for (int i = 0; i < count; i++) {
        if (i > 0) printf(" ");
        Value v = tv_to_value(args[i]);
        value_print(v);
    }
    printf("\n");
}

/* ── Strings ────────────────────────────────────────── */

TantrumsValue rt_string_from_cstr(const char* s) {
    ObjString* str = obj_string_new(s, (int)strlen(s));
    return tv_obj(str);
}

TantrumsValue rt_input(TantrumsValue prompt) {
    int tag = tv_tag(prompt);
    if (tag == TV_TAG_OBJ) {
        Obj* obj = (Obj*)tv_to_obj(prompt);
        if (obj && obj->type == OBJ_STRING) {
            printf("%s", ((ObjString*)obj)->chars);
        }
    }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return TV_NULL;
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    ObjString* s = obj_string_new(buf, len);
    return tv_obj(s);
}

/* ── Collections ────────────────────────────────────── */

TantrumsValue rt_len(TantrumsValue v) {
    int tag = tv_tag(v);
    if (tag == TV_TAG_OBJ) {
        Obj* obj = (Obj*)tv_to_obj(v);
        if (!obj) return tv_int(0);
        switch (obj->type) {
        case OBJ_STRING: return tv_int(((ObjString*)obj)->length);
        case OBJ_LIST:   return tv_int(((ObjList*)obj)->count);
        case OBJ_MAP:    return tv_int(((ObjMap*)obj)->count);
        case OBJ_RANGE:  return tv_int(((ObjRange*)obj)->length);
        default: break;
        }
    }
    return tv_int(0);
}

TantrumsValue rt_range(TantrumsValue a, TantrumsValue b, TantrumsValue c) {
    int64_t start = 0, end = 0, step = 1;
    /* Decode based on which args are null */
    if (tv_tag(b) == TV_TAG_NULL && tv_tag(c) == TV_TAG_NULL) {
        /* range(end) */
        if (tv_tag(a) != TV_TAG_INT) return tv_obj(obj_range_new(0, 0, 1));
        end = tv_to_int(a);
    } else if (tv_tag(c) == TV_TAG_NULL) {
        /* range(start, end) */
        if (tv_tag(a) != TV_TAG_INT || tv_tag(b) != TV_TAG_INT) return tv_obj(obj_range_new(0, 0, 1));
        start = tv_to_int(a);
        end = tv_to_int(b);
    } else {
        /* range(start, end, step) */
        if (tv_tag(a) != TV_TAG_INT || tv_tag(b) != TV_TAG_INT || tv_tag(c) != TV_TAG_INT)
            return tv_obj(obj_range_new(0, 0, 1));
        start = tv_to_int(a);
        end = tv_to_int(b);
        step = tv_to_int(c);
    }
    if (step == 0) return tv_obj(obj_range_new(0, 0, 1));
    return tv_obj(obj_range_new(start, end, step));
}

TantrumsValue rt_type(TantrumsValue v) {
    Value val = tv_to_value(v);
    const char* name = value_type_name(val);
    ObjString* s = obj_string_new(name, (int)strlen(name));
    return tv_obj(s);
}

TantrumsValue rt_list_new(TantrumsValue* items, int32_t count) {
    ObjList* list = obj_list_new();
    for (int i = 0; i < count; i++) {
        obj_list_append(list, tv_to_value(items[i]));
    }
    return tv_obj(list);
}

TantrumsValue rt_map_new(TantrumsValue* keys, TantrumsValue* vals, int32_t count) {
    ObjMap* map = obj_map_new();
    for (int i = 0; i < count; i++) {
        obj_map_set(map, tv_to_value(keys[i]), tv_to_value(vals[i]));
    }
    return tv_obj(map);
}

TantrumsValue rt_index_get(TantrumsValue obj_tv, TantrumsValue idx_tv) {
    Value obj = tv_to_value(obj_tv);
    Value idx = tv_to_value(idx_tv);

    if (IS_LIST(obj)) {
        ObjList* list = AS_LIST(obj);
        if (!IS_INT(idx)) {
            if (try_depth > 0) {
                caught_exception = tv_obj(obj_string_new("List index must be an integer.", 30));
                longjmp(try_stack[try_depth - 1], 1);
            }
            rt_fatal_error("List index must be an integer.");
        }
        int64_t i = AS_INT(idx);
        if (i < 0 || i >= list->count) {
            if (try_depth > 0) {
                char buf[128];
                snprintf(buf, sizeof(buf), "List index %" PRId64 " out of bounds (length %d).", i, list->count);
                caught_exception = tv_obj(obj_string_new(buf, (int)strlen(buf)));
                longjmp(try_stack[try_depth - 1], 1);
            }
            rt_fatal_error("List index %" PRId64 " out of bounds (length %d).", i, list->count);
        }
        return value_to_tv(list->items[i]);
    }
    if (IS_MAP(obj)) {
        ObjMap* map = AS_MAP(obj);
        Value out;
        if (obj_map_get(map, idx, &out)) return value_to_tv(out);
        return TV_NULL;
    }
    if (IS_STRING(obj)) {
        ObjString* str = AS_STRING(obj);
        if (!IS_INT(idx)) return TV_NULL;
        int64_t i = AS_INT(idx);
        if (i < 0 || i >= str->length) return TV_NULL;
        return tv_obj(obj_string_new(str->chars + i, 1));
    }
    if (IS_RANGE(obj)) {
        ObjRange* r = AS_RANGE(obj);
        if (!IS_INT(idx)) return TV_NULL;
        int64_t i = AS_INT(idx);
        if (i < 0 || i >= r->length) return TV_NULL;
        return tv_int(r->start + i * r->step);
    }
    return TV_NULL;
}

void rt_index_set(TantrumsValue obj_tv, TantrumsValue idx_tv, TantrumsValue val_tv) {
    Value obj = tv_to_value(obj_tv);
    Value idx = tv_to_value(idx_tv);
    Value val = tv_to_value(val_tv);

    if (IS_LIST(obj)) {
        ObjList* list = AS_LIST(obj);
        if (!IS_INT(idx)) return;
        int64_t i = AS_INT(idx);
        if (i < 0 || i >= list->count) return;
        list->items[i] = val;
    } else if (IS_MAP(obj)) {
        obj_map_set(AS_MAP(obj), idx, val);
    }
}

void rt_append(TantrumsValue list_tv, TantrumsValue val_tv) {
    Value list = tv_to_value(list_tv);
    Value val = tv_to_value(val_tv);
    if (IS_LIST(list)) {
        /* Mark pointer args as escaped */
        if (IS_OBJ(val) && AS_OBJ(val)->type == OBJ_POINTER) {
            ObjPointer* p = (ObjPointer*)AS_OBJ(val);
            p->escaped = true;
        }
        obj_list_append(AS_LIST(list), val);
    }
}

/* ── Memory / Pointers ──────────────────────────────── */

TantrumsValue rt_alloc(TantrumsValue init_tv, const char* type_name) {
    Value init = tv_to_value(init_tv);
    Value* target = (Value*)tantrums_realloc(nullptr, 0, sizeof(Value));
    *target = init;

    ObjPointer* ptr = obj_pointer_new(target);
    ptr->alloc_size = sizeof(ObjPointer) + sizeof(Value);
    ptr->alloc_line = 0;
    ptr->scope_depth = scope_depth;
    ptr->auto_manage = global_autofree;
    if (type_name) {
        ptr->alloc_type = obj_string_new(type_name, (int)strlen(type_name));
    }
    return tv_obj(ptr);
}

void rt_free_val(TantrumsValue ptr_tv) {
    if (tv_tag(ptr_tv) == TV_TAG_NULL) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Null pointer dereference on pointer!", 35));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Null pointer dereference on pointer!");
    }
    if (tv_tag(ptr_tv) != TV_TAG_OBJ) return;

    Obj* obj = (Obj*)tv_to_obj(ptr_tv);
    if (!obj || obj->type != OBJ_POINTER) return;

    ObjPointer* p = (ObjPointer*)obj;
    if (!p->is_valid) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Double-free detected: pointer has already been freed.", 53));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Double-free detected: pointer has already been freed.");
    }
    if (p->target) {
        tantrums_realloc(p->target, sizeof(Value), 0);
        p->target = nullptr;
    }
    p->is_valid = false;
}

TantrumsValue rt_ptr_deref(TantrumsValue ptr_tv) {
    if (tv_tag(ptr_tv) == TV_TAG_NULL) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Null pointer dereference on pointer!", 35));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Null pointer dereference on pointer!");
    }
    if (tv_tag(ptr_tv) != TV_TAG_OBJ) return TV_NULL;

    Obj* obj = (Obj*)tv_to_obj(ptr_tv);
    if (!obj || obj->type != OBJ_POINTER) return TV_NULL;

    ObjPointer* p = (ObjPointer*)obj;
    if (!p->is_valid || !p->target) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Null pointer dereference on pointer!", 35));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Null pointer dereference on pointer!");
    }
    return value_to_tv(*p->target);
}

void rt_ptr_set(TantrumsValue ptr_tv, TantrumsValue val_tv) {
    if (tv_tag(ptr_tv) == TV_TAG_NULL) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Null pointer dereference on pointer!", 35));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Null pointer dereference on pointer!");
    }
    if (tv_tag(ptr_tv) != TV_TAG_OBJ) return;

    Obj* obj = (Obj*)tv_to_obj(ptr_tv);
    if (!obj || obj->type != OBJ_POINTER) return;

    ObjPointer* p = (ObjPointer*)obj;
    if (!p->is_valid || !p->target) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Null pointer dereference on pointer!", 35));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Null pointer dereference on pointer!");
    }
    *p->target = tv_to_value(val_tv);
}

/* ── Arithmetic ─────────────────────────────────────── */

TantrumsValue rt_add(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);

    /* String concatenation: if either side is a string */
    if (IS_STRING(va) || IS_STRING(vb)) {
        char buf_a[4096], buf_b[4096];
        value_sprint(va, buf_a, sizeof(buf_a));
        value_sprint(vb, buf_b, sizeof(buf_b));

        int la = (int)strlen(buf_a);
        int lb = (int)strlen(buf_b);
        ObjString* sa = obj_string_new(buf_a, la);
        ObjString* sb = obj_string_new(buf_b, lb);
        ObjString* result = obj_string_concat(sa, sb);
        return tv_obj(result);
    }

    /* List/range concat */
    if ((IS_LIST(va) || IS_RANGE(va)) && (IS_LIST(vb) || IS_RANGE(vb))) {
        ObjList* result = obj_list_new();
        /* Add left side */
        if (IS_LIST(va)) {
            ObjList* la = AS_LIST(va);
            for (int i = 0; i < la->count; i++)
                obj_list_append(result, la->items[i]);
        } else {
            ObjRange* ra = AS_RANGE(va);
            for (int64_t i = 0; i < ra->length; i++)
                obj_list_append(result, INT_VAL(ra->start + i * ra->step));
        }
        /* Add right side */
        if (IS_LIST(vb)) {
            ObjList* lb = AS_LIST(vb);
            for (int i = 0; i < lb->count; i++)
                obj_list_append(result, lb->items[i]);
        } else {
            ObjRange* rb = AS_RANGE(vb);
            for (int64_t i = 0; i < rb->length; i++)
                obj_list_append(result, INT_VAL(rb->start + i * rb->step));
        }
        return tv_obj(result);
    }

    /* Numeric */
    if (IS_INT(va) && IS_INT(vb)) return tv_int(AS_INT(va) + AS_INT(vb));
    if (IS_FLOAT(va) || IS_FLOAT(vb)) {
        double da = IS_FLOAT(va) ? AS_FLOAT(va) : (double)AS_INT(va);
        double db = IS_FLOAT(vb) ? AS_FLOAT(vb) : (double)AS_INT(vb);
        return tv_float(da + db);
    }
    return TV_NULL;
}

TantrumsValue rt_sub(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    if (IS_INT(va) && IS_INT(vb)) return tv_int(AS_INT(va) - AS_INT(vb));
    if ((IS_INT(va) || IS_FLOAT(va)) && (IS_INT(vb) || IS_FLOAT(vb))) {
        double da = IS_FLOAT(va) ? AS_FLOAT(va) : (double)AS_INT(va);
        double db = IS_FLOAT(vb) ? AS_FLOAT(vb) : (double)AS_INT(vb);
        return tv_float(da - db);
    }
    return TV_NULL;
}

TantrumsValue rt_mul(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    if (IS_INT(va) && IS_INT(vb)) return tv_int(AS_INT(va) * AS_INT(vb));
    if ((IS_INT(va) || IS_FLOAT(va)) && (IS_INT(vb) || IS_FLOAT(vb))) {
        double da = IS_FLOAT(va) ? AS_FLOAT(va) : (double)AS_INT(va);
        double db = IS_FLOAT(vb) ? AS_FLOAT(vb) : (double)AS_INT(vb);
        return tv_float(da * db);
    }
    return TV_NULL;
}

TantrumsValue rt_div(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    double db = 0;
    if (IS_INT(vb))   db = (double)AS_INT(vb);
    else if (IS_FLOAT(vb)) db = AS_FLOAT(vb);
    if (db == 0.0) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Division by zero.", 17));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Division by zero.");
    }
    if (IS_INT(va) && IS_INT(vb)) return tv_int(AS_INT(va) / AS_INT(vb));
    double da = IS_FLOAT(va) ? AS_FLOAT(va) : (double)AS_INT(va);
    return tv_float(da / db);
}

TantrumsValue rt_mod(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    if (!IS_INT(va) || !IS_INT(vb)) {
        /* Modulo operands must be integers */
        if (IS_FLOAT(va) || IS_FLOAT(vb)) {
            bool ok_a = false, ok_b = false;
            double da = 0, db_d = 0;
            tv_as_number(a, &ok_a);
            tv_as_number(b, &ok_b);
            (void)da; (void)db_d;
        }
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Modulo operands must be integers.", 32));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Modulo operands must be integers.");
    }
    int64_t ib = AS_INT(vb);
    if (ib == 0) {
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new("Modulo by zero.", 15));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("Modulo by zero.");
    }
    return tv_int(AS_INT(va) % ib);
}

TantrumsValue rt_negate(TantrumsValue v) {
    int tag = tv_tag(v);
    if (tag == TV_TAG_INT)   return tv_int(-tv_to_int(v));
    if (tag == TV_TAG_FLOAT) return tv_float(-tv_to_float(v));
    return TV_NULL;
}

TantrumsValue rt_not(TantrumsValue v) {
    int tag = tv_tag(v);
    if (tag != TV_TAG_BOOL) {
        Value val = tv_to_value(v);
        const char* tname = value_type_name(val);
        char buf[256];
        snprintf(buf, sizeof(buf), "Operand of '!' must be a boolean, got %s.", tname);
        if (try_depth > 0) {
            caught_exception = tv_obj(obj_string_new(buf, (int)strlen(buf)));
            longjmp(try_stack[try_depth - 1], 1);
        }
        rt_fatal_error("%s", buf);
    }
    return tv_bool(!tv_to_bool(v));
}

/* ── Comparison ─────────────────────────────────────── */

TantrumsValue rt_eq(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    return tv_bool(value_equal(va, vb));
}

TantrumsValue rt_neq(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    return tv_bool(!value_equal(va, vb));
}

static int compare_values(TantrumsValue a, TantrumsValue b) {
    Value va = tv_to_value(a);
    Value vb = tv_to_value(b);
    double da = 0, db = 0;
    if (IS_INT(va))   da = (double)AS_INT(va);
    else if (IS_FLOAT(va)) da = AS_FLOAT(va);
    if (IS_INT(vb))   db = (double)AS_INT(vb);
    else if (IS_FLOAT(vb)) db = AS_FLOAT(vb);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

TantrumsValue rt_lt(TantrumsValue a, TantrumsValue b)  { return tv_bool(compare_values(a, b) < 0); }
TantrumsValue rt_gt(TantrumsValue a, TantrumsValue b)  { return tv_bool(compare_values(a, b) > 0); }
TantrumsValue rt_lte(TantrumsValue a, TantrumsValue b) { return tv_bool(compare_values(a, b) <= 0); }
TantrumsValue rt_gte(TantrumsValue a, TantrumsValue b) { return tv_bool(compare_values(a, b) >= 0); }

/* ── Truthiness ─────────────────────────────────────── */

int32_t rt_is_truthy(TantrumsValue v) {
    int tag = tv_tag(v);
    if (tag == TV_TAG_BOOL)  return tv_to_bool(v) ? 1 : 0;
    if (tag == TV_TAG_NULL)  return 0;
    /* Non-bool in condition → runtime error */
    Value val = tv_to_value(v);
    const char* tname = value_type_name(val);
    char buf[256];
    snprintf(buf, sizeof(buf), "Condition must be a boolean, got %s.", tname);
    if (try_depth > 0) {
        caught_exception = tv_obj(obj_string_new(buf, (int)strlen(buf)));
        longjmp(try_stack[try_depth - 1], 1);
    }
    rt_fatal_error("%s", buf);
    return 0; /* unreachable */
}

/* ── For-in loop support ────────────────────────────── */

int32_t rt_for_in_has_next(TantrumsValue iterable, int64_t idx) {
    Value v = tv_to_value(iterable);
    if (IS_RANGE(v)) return idx < AS_RANGE(v)->length ? 1 : 0;
    if (IS_LIST(v))  return idx < AS_LIST(v)->count ? 1 : 0;
    if (IS_STRING(v)) return idx < AS_STRING(v)->length ? 1 : 0;
    if (IS_MAP(v)) {
        ObjMap* map = AS_MAP(v);
        int64_t seen = 0;
        for (int i = 0; i < map->capacity; i++) {
            if (map->entries[i].occupied) {
                if (seen == idx) return 1;
                seen++;
            }
        }
        return 0;
    }
    return 0;
}

TantrumsValue rt_for_in_step(TantrumsValue iterable, int64_t* counter) {
    int64_t idx = *counter;
    (*counter)++;
    Value v = tv_to_value(iterable);
    if (IS_RANGE(v)) {
        ObjRange* r = AS_RANGE(v);
        return tv_int(r->start + idx * r->step);
    }
    if (IS_LIST(v)) {
        ObjList* list = AS_LIST(v);
        if (idx < list->count) return value_to_tv(list->items[idx]);
        return TV_NULL;
    }
    if (IS_STRING(v)) {
        ObjString* s = AS_STRING(v);
        if (idx < s->length) return tv_obj(obj_string_new(s->chars + idx, 1));
        return TV_NULL;
    }
    if (IS_MAP(v)) {
        ObjMap* map = AS_MAP(v);
        int64_t seen = 0;
        for (int i = 0; i < map->capacity; i++) {
            if (map->entries[i].occupied) {
                if (seen == idx) return value_to_tv(map->entries[i].key);
                seen++;
            }
        }
        return TV_NULL;
    }
    return TV_NULL;
}

/* ── Error handling ─────────────────────────────────── */

void rt_throw(TantrumsValue val) {
    if (try_depth > 0) {
        caught_exception = val;
        longjmp(try_stack[try_depth - 1], 1);
    }
    /* Uncaught throw */
    Value v = tv_to_value(val);
    char msg[4096];
    value_sprint(v, msg, sizeof(msg));
    fprintf(stderr, "[Tantrums Error] %s\n", msg);
    exit(1);
}

int32_t rt_try_enter(void) {
    if (try_depth >= MAX_TRY_DEPTH) {
        rt_fatal_error("Too many nested try blocks.");
    }
    if (setjmp(try_stack[try_depth]) != 0) {
        /* Landed here from longjmp (exception caught) */
        return 1;
    }
    try_depth++;
    return 0;
}

void rt_try_exit(void) {
    if (try_depth > 0) try_depth--;
}

TantrumsValue rt_caught_val(void) {
    return caught_exception;
}

/* ── Type casting ───────────────────────────────────── */

TantrumsValue rt_cast(TantrumsValue v, int32_t target) {
    /* target: 0=int, 1=float, 2=string, 3=bool */
    Value val = tv_to_value(v);
    switch (target) {
    case 0: { /* int */
        if (IS_INT(val)) return v;
        if (IS_FLOAT(val)) return tv_int((int64_t)AS_FLOAT(val));
        if (IS_BOOL(val)) return tv_int(AS_BOOL(val) ? 1 : 0);
        if (IS_STRING(val)) {
            int64_t n = strtoll(AS_CSTRING(val), nullptr, 10);
            return tv_int(n);
        }
        return tv_int(0);
    }
    case 1: { /* float */
        if (IS_FLOAT(val)) return v;
        if (IS_INT(val)) return tv_float((double)AS_INT(val));
        if (IS_BOOL(val)) return tv_float(AS_BOOL(val) ? 1.0 : 0.0);
        if (IS_STRING(val)) {
            double d = strtod(AS_CSTRING(val), nullptr);
            return tv_float(d);
        }
        return tv_float(0.0);
    }
    case 2: { /* string */
        if (IS_STRING(val)) return v;
        char buf[4096];
        value_sprint(val, buf, sizeof(buf));
        ObjString* s = obj_string_new(buf, (int)strlen(buf));
        return tv_obj(s);
    }
    case 3: { /* bool */
        if (IS_BOOL(val)) return v;
        if (IS_INT(val)) return tv_bool(AS_INT(val) != 0);
        if (IS_FLOAT(val)) return tv_bool(AS_FLOAT(val) != 0.0);
        if (IS_NULL(val)) return TV_FALSE;
        if (IS_STRING(val)) {
            const char* s = AS_CSTRING(val);
            if (strcmp(s, "false") == 0) return TV_FALSE;
            /* Empty or whitespace-only → false */
            bool all_ws = true;
            for (int i = 0; s[i]; i++) {
                if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
                    all_ws = false;
                    break;
                }
            }
            return tv_bool(!all_ws && s[0] != '\0');
        }
        return TV_TRUE;
    }
    default: return v;
    }
}

/* ── Scope tracking ─────────────────────────────────── */

void rt_enter_scope(void) {
    if (scope_depth < MAX_SCOPES) {
        scope_markers[scope_depth] = all_objects;
    }
    scope_depth++;
}

void rt_exit_scope(void) {
    scope_depth--;
    if (scope_depth < 0) scope_depth = 0;

    if (!global_autofree) return;

    /* Walk objects created since scope entry and free locals */
    Obj* marker = (scope_depth < MAX_SCOPES) ? scope_markers[scope_depth] : nullptr;
    for (Obj* obj = all_objects; obj != nullptr && obj != marker; obj = obj->next) {
        if (obj->type == OBJ_POINTER) {
            ObjPointer* p = (ObjPointer*)obj;
            if (!p->escaped && p->auto_manage && p->is_valid && p->scope_depth > scope_depth) {
                if (p->target) {
                    tantrums_realloc(p->target, sizeof(Value), 0);
                    p->target = nullptr;
                }
                p->is_valid = false;
                total_auto_frees++;
            }
        } else if (obj->type == OBJ_LIST) {
            ObjList* l = (ObjList*)obj;
            if (!l->escaped && l->auto_manage && l->scope_depth > scope_depth) {
                l->auto_manage = false; /* only free once */
            }
        } else if (obj->type == OBJ_MAP) {
            ObjMap* m = (ObjMap*)obj;
            if (!m->escaped && m->auto_manage && m->scope_depth > scope_depth) {
                m->auto_manage = false;
            }
        }
    }
}

void rt_mark_escaped(TantrumsValue v) {
    if (tv_tag(v) != TV_TAG_OBJ) return;
    Obj* obj = (Obj*)tv_to_obj(v);
    if (!obj) return;
    if (obj->type == OBJ_POINTER) ((ObjPointer*)obj)->escaped = true;
    else if (obj->type == OBJ_LIST) ((ObjList*)obj)->escaped = true;
    else if (obj->type == OBJ_MAP) ((ObjMap*)obj)->escaped = true;
}

void rt_free_collection(TantrumsValue v) {
    /* Silent free of local list/map */
    if (tv_tag(v) != TV_TAG_OBJ) return;
    (void)v; /* The GC will handle actual deallocation */
}

/* ── Time API ───────────────────────────────────────── */

TantrumsValue rt_getCurrentTime(void) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return tv_int((int64_t)ms);
}

TantrumsValue rt_toSeconds(TantrumsValue ms) {
    bool ok; double d = tv_as_number(ms, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(d / 1000.0);
}

TantrumsValue rt_toMilliseconds(TantrumsValue ms) {
    bool ok; double d = tv_as_number(ms, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(d);
}

TantrumsValue rt_toMinutes(TantrumsValue ms) {
    bool ok; double d = tv_as_number(ms, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(d / 60000.0);
}

TantrumsValue rt_toHours(TantrumsValue ms) {
    bool ok; double d = tv_as_number(ms, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(d / 3600000.0);
}

/* ── Memory API ─────────────────────────────────────── */

TantrumsValue rt_getProcessMemory(void) {
    size_t mem = 0;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        mem = pmc.WorkingSetSize;
    }
#elif defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        mem = (size_t)info.resident_size;
    }
#elif defined(__linux__)
    long rss = 0L;
    FILE* fp = fopen("/proc/self/statm", "r");
    if (fp) {
        if (fscanf(fp, "%*s%ld", &rss) == 1) {
            mem = (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
        }
        fclose(fp);
    }
#endif
    return tv_int((int64_t)mem);
}

TantrumsValue rt_getHeapMemory(void) {
    return tv_int((int64_t)tantrums_bytes_allocated);
}

TantrumsValue rt_getHeapPeakMemory(void) {
    return tv_int((int64_t)tantrums_peak_bytes_allocated);
}

TantrumsValue rt_bytesToKB(TantrumsValue bytes) {
    bool ok; double b = tv_as_number(bytes, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(b / 1024.0);
}

TantrumsValue rt_bytesToMB(TantrumsValue bytes) {
    bool ok; double b = tv_as_number(bytes, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(b / (1024.0 * 1024.0));
}

TantrumsValue rt_bytesToGB(TantrumsValue bytes) {
    bool ok; double b = tv_as_number(bytes, &ok);
    if (!ok) return tv_float(0.0);
    return tv_float(b / (1024.0 * 1024.0 * 1024.0));
}

} /* extern "C" */
