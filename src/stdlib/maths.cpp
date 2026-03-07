#include "stdlib/maths.h"
#include "value.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

/* ── NaN-boxing Helpers for ABI ── */

static Value tv_to_value_math(TantrumsValue tv) {
    switch (tv_tag(tv)) {
    case TV_TAG_INT:   return INT_VAL(tv_to_int(tv));
    case TV_TAG_FLOAT: return FLOAT_VAL(tv_to_float(tv));
    case TV_TAG_BOOL:  return BOOL_VAL(tv_to_bool(tv));
    case TV_TAG_NULL:  return NULL_VAL;
    case TV_TAG_OBJ:   return OBJ_VAL(tv_to_obj(tv));
    default:           return NULL_VAL;
    }
}

static TantrumsValue value_to_tv_math(Value v) {
    switch (v.type) {
    case VAL_INT:   return tv_int(v.as.integer);
    case VAL_FLOAT: return tv_float(v.as.floating);
    case VAL_BOOL:  return tv_bool(v.as.boolean);
    case VAL_NULL:  return TV_NULL;
    case VAL_OBJ:   return tv_obj(v.as.obj);
    default:        return TV_NULL;
    }
}

static inline bool get_number(Value v, double* out) {
    if (IS_INT(v))   { *out = (double)AS_INT(v);   return true; }
    if (IS_FLOAT(v)) { *out = AS_FLOAT(v);         return true; }
    return false;
}

extern "C" {
    TantrumsValue rt_math_sin(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::sin(d)));
    }
    TantrumsValue rt_math_cos(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::cos(d)));
    }
    TantrumsValue rt_math_tan(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::tan(d)));
    }
    TantrumsValue rt_math_sec(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(1.0 / std::cos(d)));
    }
    TantrumsValue rt_math_cosec(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(1.0 / std::sin(d)));
    }
    TantrumsValue rt_math_cot(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(1.0 / std::tan(d)));
    }
    TantrumsValue rt_math_floor(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::floor(d)));
    }
    TantrumsValue rt_math_ceil(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::ceil(d)));
    }
    TantrumsValue rt_math_random_int(TantrumsValue min_tv, TantrumsValue max_tv) {
        Value min_val = tv_to_value_math(min_tv);
        Value max_val = tv_to_value_math(max_tv);
        if (!IS_INT(min_val) || !IS_INT(max_val)) return value_to_tv_math(INT_VAL(0));
        int64_t mn = AS_INT(min_val);
        int64_t mx = AS_INT(max_val);
        if (mn > mx) { int64_t t = mn; mn = mx; mx = t; }
        int64_t range = mx - mn + 1;
        int64_t r = range > 0 ? (rand() % range) + mn : mn;
        return value_to_tv_math(INT_VAL(r));
    }
    TantrumsValue rt_math_random_float(TantrumsValue min_tv, TantrumsValue max_tv) {
        Value min_val = tv_to_value_math(min_tv);
        Value max_val = tv_to_value_math(max_tv);
        double mn, mx;
        if (!get_number(min_val, &mn) || !get_number(max_val, &mx)) return value_to_tv_math(FLOAT_VAL(0.0));
        if (mn > mx) { double t = mn; mn = mx; mx = t; }
        double r = (double)rand() / RAND_MAX;
        return value_to_tv_math(FLOAT_VAL(mn + r * (mx - mn)));
    }
    TantrumsValue rt_math_sqrt(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::sqrt(d)));
    }
    TantrumsValue rt_math_pow(TantrumsValue base_tv, TantrumsValue exp_tv) {
        Value base_val = tv_to_value_math(base_tv);
        Value exp_val = tv_to_value_math(exp_tv);
        double b, e;
        if (!get_number(base_val, &b) || !get_number(exp_val, &e)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::pow(b, e)));
    }
    TantrumsValue rt_math_cbrt(TantrumsValue x_tv) {
        Value x = tv_to_value_math(x_tv);
        double d;
        if (!get_number(x, &d)) return value_to_tv_math(FLOAT_VAL(0.0));
        return value_to_tv_math(FLOAT_VAL(std::cbrt(d)));
    }
}
