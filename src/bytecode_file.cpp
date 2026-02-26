#include "bytecode_file.h"
#include "chunk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ── Little-endian helpers (cross-platform) ───────── */
static void write_u8(FILE* f, uint8_t v)   { fwrite(&v, 1, 1, f); }
static void write_u32(FILE* f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static void write_i64(FILE* f, int64_t v) {
    uint64_t u = (uint64_t)v;
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(u >> (i * 8));
    fwrite(b, 1, 8, f);
}
static void write_f64(FILE* f, double v)   { fwrite(&v, sizeof(double), 1, f); }
static void write_i32(FILE* f, int32_t v) {
    uint32_t u = (uint32_t)v;
    uint8_t b[4] = { (uint8_t)(u), (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24) };
    fwrite(b, 1, 4, f);
}

static uint8_t read_u8(FILE* f)   { uint8_t v = 0; fread(&v, 1, 1, f); return v; }
static uint32_t read_u32(FILE* f) {
    uint8_t b[4] = {0}; fread(b, 1, 4, f);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static int64_t read_i64(FILE* f) {
    uint8_t b[8] = {0}; fread(b, 1, 8, f);
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u |= ((uint64_t)b[i] << (i * 8));
    return (int64_t)u;
}
static double read_f64(FILE* f) { double v = 0; fread(&v, sizeof(double), 1, f); return v; }
static int32_t read_i32(FILE* f) {
    uint8_t b[4] = {0}; fread(b, 1, 4, f);
    uint32_t u = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return (int32_t)u;
}

/* ── Write a function (recursive for nested functions) ── */
static void write_function(FILE* f, ObjFunction* fn) {
    /* Name */
    if (fn->name) {
        write_u32(f, (uint32_t)fn->name->length);
        fwrite(fn->name->chars, 1, fn->name->length, f);
    } else {
        write_u32(f, 0);
    }

    /* Arity */
    write_u32(f, (uint32_t)fn->arity);

    /* Constants */
    Chunk* c = fn->chunk;
    write_u32(f, (uint32_t)c->const_count);
    for (int i = 0; i < c->const_count; i++) {
        Value v = c->constants[i];
        if (IS_INT(v)) {
            write_u8(f, 0);
            write_i64(f, AS_INT(v));
        } else if (IS_FLOAT(v)) {
            write_u8(f, 1);
            write_f64(f, AS_FLOAT(v));
        } else if (IS_STRING(v)) {
            write_u8(f, 2);
            ObjString* s = AS_STRING(v);
            write_u32(f, (uint32_t)s->length);
            fwrite(s->chars, 1, s->length, f);
        } else if (IS_BOOL(v)) {
            write_u8(f, AS_BOOL(v) ? 3 : 4);
        } else if (IS_NULL(v)) {
            write_u8(f, 5);
        } else if (IS_FUNCTION(v)) {
            write_u8(f, 6);
            write_function(f, AS_FUNCTION(v));
        } else {
            write_u8(f, 5); /* fallback: null */
        }
    }

    /* Bytecode */
    write_u32(f, (uint32_t)c->count);
    fwrite(c->code, 1, c->count, f);

    /* Line info */
    write_u32(f, (uint32_t)c->count);
    for (int i = 0; i < c->count; i++) {
        write_i32(f, (int32_t)c->lines[i]);
    }
}

/* ── Read a function (recursive) ──────────────────── */
static ObjFunction* read_function(FILE* f) {
    ObjFunction* fn = obj_function_new();

    /* Name */
    uint32_t name_len = read_u32(f);
    if (name_len > 0) {
        char* buf = (char*)malloc(name_len + 1);
        fread(buf, 1, name_len, f);
        buf[name_len] = '\0';
        fn->name = obj_string_new(buf, (int)name_len);
        free(buf);
    }

    /* Arity */
    fn->arity = (int)read_u32(f);

    /* Constants */
    uint32_t const_count = read_u32(f);
    for (uint32_t i = 0; i < const_count; i++) {
        uint8_t tag = read_u8(f);
        Value v;
        switch (tag) {
        case 0: v = INT_VAL(read_i64(f)); break;
        case 1: v = FLOAT_VAL(read_f64(f)); break;
        case 2: {
            uint32_t slen = read_u32(f);
            char* buf = (char*)malloc(slen + 1);
            fread(buf, 1, slen, f);
            buf[slen] = '\0';
            v = OBJ_VAL(obj_string_new(buf, (int)slen));
            free(buf);
        } break;
        case 3: v = BOOL_VAL(true); break;
        case 4: v = BOOL_VAL(false); break;
        case 5: v = NULL_VAL; break;
        case 6: v = OBJ_VAL(read_function(f)); break;
        default: v = NULL_VAL; break;
        }
        chunk_add_constant(fn->chunk, v);
    }

    /* Bytecode */
    uint32_t code_len = read_u32(f);
    for (uint32_t i = 0; i < code_len; i++) {
        uint8_t byte = read_u8(f);
        chunk_write(fn->chunk, byte, 0); /* line patched below */
    }

    /* Line info */
    uint32_t line_count = read_u32(f);
    for (uint32_t i = 0; i < line_count && i < (uint32_t)fn->chunk->count; i++) {
        fn->chunk->lines[i] = read_i32(f);
    }

    return fn;
}

/* ── Public API ───────────────────────────────────── */
bool bytecode_write(const char* path, ObjFunction* script) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Could not write bytecode file '%s'.\n", path);
        return false;
    }

    /* Header */
    fwrite(BYTECODE_MAGIC, 1, 4, f);
    write_u8(f, BYTECODE_VERSION);

    /* Script function (contains all others as constants) */
    write_function(f, script);

    fclose(f);
    return true;
}

ObjFunction* bytecode_read(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open bytecode file '%s'.\n", path);
        return nullptr;
    }

    /* Verify header */
    char magic[4] = {0};
    fread(magic, 1, 4, f);
    if (memcmp(magic, BYTECODE_MAGIC, 4) != 0) {
        fprintf(stderr, "'%s' is not a valid .42ass bytecode file.\n", path);
        fclose(f);
        return nullptr;
    }
    uint8_t ver = read_u8(f);
    if (ver != BYTECODE_VERSION) {
        fprintf(stderr, "Bytecode version %d not supported (expected %d).\n", ver, BYTECODE_VERSION);
        fclose(f);
        return nullptr;
    }

    ObjFunction* script = read_function(f);
    fclose(f);
    return script;
}
