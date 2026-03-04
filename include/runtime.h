/*  runtime.h  —  NaN-boxed TantrumsValue (uint64_t) + runtime ABI
 *
 *  Every value that crosses the IR ↔ runtime boundary is a plain i64.
 *  On Windows x64 MSVC ABI this returns in RAX — no sret, no hidden ptr.
 */
#ifndef TANTRUMS_RUNTIME_H
#define TANTRUMS_RUNTIME_H

#include <cstdint>
#include <cstring>

/* ─────────────────────────── NaN-boxing ─────────────────────────── */

typedef uint64_t TantrumsValue;

/*  Layout (64 bits):
 *    Bits 63-51  : 0xFFF8 >> 3 bits = NaN marker (quiet NaN, sign=1)
 *    Bits 50-48  : tag  (3 bits → 8 possible tags)
 *    Bits 47-0   : payload (48 bits)
 *
 *  Floats are stored as plain IEEE-754 bits.  A float whose bits
 *  happen to match the NaN range is canonicalized to TV_NAN_BASE.
 */

#define TV_NAN_BASE  0xFFF8000000000000ULL

#define TV_TAG_FLOAT 0x0   /* not stored via TV_MAKE – raw double bits */
#define TV_TAG_NULL  0x1
#define TV_TAG_INT   0x2   /* payload = lower 48 bits of int64          */
#define TV_TAG_BOOL  0x3   /* payload = 0 or 1                         */
#define TV_TAG_OBJ   0x4   /* payload = lower 48 bits of pointer       */

#define TV_MAKE(tag, payload) \
    (TV_NAN_BASE | ((uint64_t)(tag) << 48) | ((uint64_t)(payload) & 0x0000FFFFFFFFFFFFULL))

#define TV_NULL  TV_MAKE(TV_TAG_NULL, 0)
#define TV_TRUE  TV_MAKE(TV_TAG_BOOL, 1)
#define TV_FALSE TV_MAKE(TV_TAG_BOOL, 0)

static inline TantrumsValue tv_int(int64_t n)  { return TV_MAKE(TV_TAG_INT, (uint64_t)n); }
static inline TantrumsValue tv_bool(bool b)    { return b ? TV_TRUE : TV_FALSE; }
static inline TantrumsValue tv_obj(void* p)    { return TV_MAKE(TV_TAG_OBJ, (uintptr_t)p); }

static inline TantrumsValue tv_float(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    /* Any real NaN that collides with our tag space → canonical NaN */
    if ((bits & TV_NAN_BASE) == TV_NAN_BASE)
        return TV_NAN_BASE;            /* canonical NaN (tag=FLOAT, payload=0) */
    return bits;
}

static inline int tv_tag(TantrumsValue v) {
    if ((v & TV_NAN_BASE) != TV_NAN_BASE) return TV_TAG_FLOAT;
    return (int)((v >> 48) & 0x7);
}

static inline double  tv_to_float(TantrumsValue v) { double d; memcpy(&d, &v, 8); return d; }

static inline int64_t tv_to_int(TantrumsValue v)   {
    uint64_t r = v & 0x0000FFFFFFFFFFFFULL;
    if (r & 0x0000800000000000ULL) r |= 0xFFFF000000000000ULL; /* sign-extend 48→64 */
    return (int64_t)r;
}

static inline bool    tv_to_bool(TantrumsValue v)  { return (v & 1) != 0; }
static inline void*   tv_to_obj(TantrumsValue v)   { return (void*)(uintptr_t)(v & 0x0000FFFFFFFFFFFFULL); }

/* ─────────── Runtime function ABI (called from generated IR) ──────────── */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Output ─────────────────────────────────────────── */
void            rt_print(TantrumsValue* args, int32_t count);

/* ── Strings ────────────────────────────────────────── */
TantrumsValue   rt_string_from_cstr(const char* s);
TantrumsValue   rt_input(TantrumsValue prompt);

/* ── Collections ────────────────────────────────────── */
TantrumsValue   rt_len(TantrumsValue v);
TantrumsValue   rt_range(TantrumsValue a, TantrumsValue b, TantrumsValue c);
TantrumsValue   rt_type(TantrumsValue v);
TantrumsValue   rt_list_new(TantrumsValue* items, int32_t count);
TantrumsValue   rt_map_new(TantrumsValue* keys, TantrumsValue* vals, int32_t count);
TantrumsValue   rt_index_get(TantrumsValue obj, TantrumsValue idx);
void            rt_index_set(TantrumsValue obj, TantrumsValue idx, TantrumsValue val);
void            rt_append(TantrumsValue list, TantrumsValue val);

/* ── Memory / Pointers ──────────────────────────────── */
TantrumsValue   rt_alloc(TantrumsValue init, const char* type_name);
void            rt_free_val(TantrumsValue ptr);
TantrumsValue   rt_ptr_deref(TantrumsValue ptr);
void            rt_ptr_set(TantrumsValue ptr, TantrumsValue val);

/* ── Arithmetic ─────────────────────────────────────── */
TantrumsValue   rt_add(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_sub(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_mul(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_div(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_mod(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_negate(TantrumsValue v);
TantrumsValue   rt_not(TantrumsValue v);

/* ── Comparison ─────────────────────────────────────── */
TantrumsValue   rt_eq(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_neq(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_lt(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_gt(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_lte(TantrumsValue a, TantrumsValue b);
TantrumsValue   rt_gte(TantrumsValue a, TantrumsValue b);

/* ── Truthiness ─────────────────────────────────────── */
int32_t         rt_is_truthy(TantrumsValue v);

/* ── For-in loop support ────────────────────────────── */
TantrumsValue   rt_for_in_step(TantrumsValue iterable, int64_t* counter);
int32_t         rt_for_in_has_next(TantrumsValue iterable, int64_t idx);

/* ── Error handling ─────────────────────────────────── */
void            rt_throw(TantrumsValue val);
int32_t         rt_try_enter(void);
void            rt_try_exit(void);
TantrumsValue   rt_caught_val(void);

/* ── Type casting ───────────────────────────────────── */
TantrumsValue   rt_cast(TantrumsValue v, int32_t target_type);

/* ── Scope tracking (memory Layer 2) ────────────────── */
void            rt_enter_scope(void);
void            rt_exit_scope(void);
void            rt_mark_escaped(TantrumsValue v);
void            rt_free_collection(TantrumsValue v);

/* ── Lifecycle ──────────────────────────────────────── */
void            rt_init(void);
void            rt_shutdown(void);

/* ── Time API ───────────────────────────────────────── */
TantrumsValue   rt_getCurrentTime(void);
TantrumsValue   rt_toSeconds(TantrumsValue ms);
TantrumsValue   rt_toMilliseconds(TantrumsValue ms);
TantrumsValue   rt_toMinutes(TantrumsValue ms);
TantrumsValue   rt_toHours(TantrumsValue ms);

/* ── Memory API ─────────────────────────────────────── */
TantrumsValue   rt_getProcessMemory(void);
TantrumsValue   rt_getHeapMemory(void);
TantrumsValue   rt_getHeapPeakMemory(void);
TantrumsValue   rt_bytesToKB(TantrumsValue bytes);
TantrumsValue   rt_bytesToMB(TantrumsValue bytes);
TantrumsValue   rt_bytesToGB(TantrumsValue bytes);

#ifdef __cplusplus
}
#endif

#endif /* TANTRUMS_RUNTIME_H */
