#include "builtins.h"
#include "memory.h"
#include <cstdio>
#include <cstring>
#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(__linux__)
#include <unistd.h>
#include <stdio.h>
#endif

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

/* range([start], end, [step]) — returns list [start, start+step, ..., end) */
static Value native_range(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || argc > 3) return OBJ_VAL(obj_list_new());

    int64_t start = 0;
    int64_t end = 0;
    int64_t step = 1;

    if (argc == 1) {
        if (!IS_INT(args[0])) return OBJ_VAL(obj_list_new());
        end = AS_INT(args[0]);
    } else if (argc == 2) {
        if (!IS_INT(args[0]) || !IS_INT(args[1])) return OBJ_VAL(obj_list_new());
        start = AS_INT(args[0]);
        end = AS_INT(args[1]);
    } else if (argc == 3) {
        if (!IS_INT(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return OBJ_VAL(obj_list_new());
        start = AS_INT(args[0]);
        end = AS_INT(args[1]);
        step = AS_INT(args[2]);
    }

    if (step == 0) return OBJ_VAL(obj_list_new()); // Prevent infinite loops

    ObjList* list = obj_list_new();
    
    if (step > 0) {
        for (int64_t i = start; i < end; i += step) {
            obj_list_append(list, INT_VAL(i));
        }
    } else {
        for (int64_t i = start; i > end; i += step) {
            obj_list_append(list, INT_VAL(i));
        }
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

/* ── Time API ─────────────────────────────────────── */

/* getCurrentTime() -> int (milliseconds sequence Unix Epoch) */
static Value native_getCurrentTime(VM* vm, int argc, Value* args) {
    (void)vm; (void)argc; (void)args;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return INT_VAL((int64_t)ms);
}

/* toSeconds(delta_ms) -> float */
static Value native_toSeconds(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / 1000.0);
}

/* toMilliseconds(delta_ms) -> int */
static Value native_toMilliseconds(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return INT_VAL(0);
    return INT_VAL(AS_INT(args[0]));
}

/* toMinutes(delta_ms) -> float */
static Value native_toMinutes(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / 60000.0);
}

/* toHours(delta_ms) -> float */
static Value native_toHours(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / 3600000.0);
}

/* ── Memory API ────────────────────────────────────── */

/* getProcessMemory() -> int */
static Value native_getProcessMemory(VM* vm, int argc, Value* args) {
    (void)vm; (void)argc; (void)args;
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
    FILE* fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) != NULL) {
        if (fscanf(fp, "%*s%ld", &rss) == 1) {
            mem = (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
        }
        fclose(fp);
    }
#endif
    return INT_VAL((int64_t)mem);
}

/* getVmMemory() -> int */
static Value native_getVmMemory(VM* vm, int argc, Value* args) {
    (void)vm; (void)argc; (void)args;
    return INT_VAL((int64_t)tantrums_bytes_allocated);
}

/* getVmPeakMemory() -> int */
static Value native_getVmPeakMemory(VM* vm, int argc, Value* args) {
    (void)vm; (void)argc; (void)args;
    return INT_VAL((int64_t)tantrums_peak_bytes_allocated);
}

/* bytesToKB(bytes) -> float */
static Value native_bytesToKB(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / 1024.0);
}

/* bytesToMB(bytes) -> float */
static Value native_bytesToMB(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / (1024.0 * 1024.0));
}

/* bytesToGB(bytes) -> float */
static Value native_bytesToGB(VM* vm, int argc, Value* args) {
    (void)vm;
    if (argc < 1 || !IS_INT(args[0])) return FLOAT_VAL(0.0);
    return FLOAT_VAL((double)AS_INT(args[0]) / (1024.0 * 1024.0 * 1024.0));
}

static void define_native(VM* vm, const char* name, NativeFn fn);

void builtins_register(VM* vm) {
    define_native(vm, "print",  native_print);
    define_native(vm, "input",  native_input);
    define_native(vm, "len",    native_len);
    define_native(vm, "range",  native_range);
    define_native(vm, "type",   native_type);
    define_native(vm, "append", native_append);
    
    // Time API
    define_native(vm, "getCurrentTime", native_getCurrentTime);
    define_native(vm, "toSeconds",      native_toSeconds);
    define_native(vm, "toMilliseconds", native_toMilliseconds);
    define_native(vm, "toMinutes",      native_toMinutes);
    define_native(vm, "toHours",        native_toHours);
    
    // Memory API
    define_native(vm, "getProcessMemory", native_getProcessMemory);
    define_native(vm, "getVmMemory",      native_getVmMemory);
    define_native(vm, "getVmPeakMemory",  native_getVmPeakMemory);
    define_native(vm, "bytesToKB",        native_bytesToKB);
    define_native(vm, "bytesToMB",        native_bytesToMB);
    define_native(vm, "bytesToGB",        native_bytesToGB);
}

static void define_native(VM* vm, const char* name, NativeFn fn) {
    ObjString* s = obj_string_new(name, (int)strlen(name));
    ObjNative* native = obj_native_new(fn, name);
    table_set(&vm->globals, s, OBJ_VAL(native));
}
