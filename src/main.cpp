/*  main.cpp  —  Tantrums CLI entry point
 *
 *  Commands:
 *    tantrums build <file.42AHH | file.trinitrotoluene>    Compile to native executable via LLVM
 *    tantrums compile <file.42AHH | file.trinitrotoluene>  Compile to .42ass bytecode (legacy)
 *    tantrums run <file.42AHH | file.trinitrotoluene>      Compile to native + run immediately
 *
 *  Flags (before filename):
 *    --no-autofree-notes   Suppress auto-free notes on stdout
 */
#include "compiler.h"
#include "LLVMCodegen.h"
#include "lexer.h"
#include "parser.h"
#include "bytecode_file.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char* current_bytecode_path = nullptr;
bool suppress_autofree_notes = false;
bool global_autofree = true;
bool global_allow_leaks = false;

/* ── File reading ──────────────────────────────────── */

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open file '%s'.\n", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char* buf = (char*)malloc(size + 1);
    size_t rd = fread(buf, 1, size, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* ── Path utilities ────────────────────────────────── */

static char* get_dir(const char* path) {
    const char* last_slash = strrchr(path, '/');
    const char* last_bslash = strrchr(path, '\\');
    const char* sep = last_slash > last_bslash ? last_slash : last_bslash;
    if (!sep) {
        char* out = (char*)malloc(1);
        out[0] = '\0';
        return out;
    }
    size_t len = sep - path + 1;
    char* out = (char*)malloc(len + 1);
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char* resolve_import_path(const char* importer_path, const char* import_path) {
    if (import_path[0] == '/' || import_path[0] == '\\' ||
        (import_path[1] == ':')) {
        char* out = (char*)malloc(strlen(import_path) + 1);
        strcpy(out, import_path);
        return out;
    }
    char* dir = get_dir(importer_path);
    size_t dir_len = strlen(dir);
    size_t imp_len = strlen(import_path);
    char* out = (char*)malloc(dir_len + imp_len + 1);
    memcpy(out, dir, dir_len);
    memcpy(out + dir_len, import_path, imp_len);
    out[dir_len + imp_len] = '\0';
    free(dir);
    return out;
}

static CompileMode strip_mode(char* source) {
    CompileMode mode = MODE_BOTH;
    char* mode_line = source;
    while ((mode_line = strstr(mode_line, "#mode "))) {
        if (strncmp(mode_line + 6, "static", 6) == 0)  mode = MODE_STATIC;
        else if (strncmp(mode_line + 6, "dynamic", 7) == 0) mode = MODE_DYNAMIC;
        else if (strncmp(mode_line + 6, "both", 4) == 0) mode = MODE_BOTH;
        char* end = strchr(mode_line, '\n');
        if (!end) end = mode_line + strlen(mode_line);
        memset(mode_line, ' ', end - mode_line);
        mode_line = end;
    }
    return mode;
}

struct MemoryDirectives {
    int autofree;
    int allow_leaks;
};

static MemoryDirectives strip_memory_directives(char* source) {
    MemoryDirectives d;
    d.autofree = -1;
    d.allow_leaks = -1;
    char* p = source;
    while ((p = strchr(p, '#')) != nullptr) {
        if (strncmp(p, "#autoFree", 9) == 0) {
            char* after = p + 9;
            while (*after == ' ' || *after == '\t') after++;
            if (strncmp(after, "true", 4) == 0) d.autofree = 1;
            else if (strncmp(after, "false", 5) == 0) d.autofree = 0;
            char* end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            memset(p, ' ', end - p);
            p = end;
        } else if (strncmp(p, "#allowMemoryLeaks", 17) == 0) {
            char* after = p + 17;
            while (*after == ' ' || *after == '\t') after++;
            if (strncmp(after, "true", 4) == 0) d.allow_leaks = 1;
            else if (strncmp(after, "false", 5) == 0) d.allow_leaks = 0;
            char* end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            memset(p, ' ', end - p);
            p = end;
        } else {
            p++;
        }
    }
    return d;
}

/* ── Prepare AST (parse + resolve imports) ─────────── */

static ASTNode* prepare_ast(char* source, const char* source_path, CompileMode* out_mode) {
    CompileMode mode = strip_mode(source);
    *out_mode = mode;

    if (mode == MODE_STATIC)
        printf("[Tantrums] Mode: static (all variables must have types)\n");
    else if (mode == MODE_DYNAMIC)
        printf("[Tantrums] Mode: dynamic (no type checking)\n");
    else
        printf("[Tantrums] Mode: both (typed + dynamic)\n");

    Lexer lexer;
    lexer_init(&lexer, source);
    TokenList tokens = lexer_scan_tokens(&lexer);

    for (int i = 0; i < tokens.count; i++) {
        if (tokens.tokens[i].type == TOKEN_ERROR) {
            fprintf(stderr, "[Line %d] Lexer error: %.*s\n",
                    tokens.tokens[i].line, tokens.tokens[i].length, tokens.tokens[i].start);
            tokenlist_free(&tokens);
            return nullptr;
        }
    }

    ASTNode* ast = parser_parse(&tokens);
    tokenlist_free(&tokens);
    if (!ast) return nullptr;

    /* ── Resolve imports ── */
    if (ast->type == NODE_PROGRAM) {
        char* imported_files[64];
        int import_count = 0;

        for (int i = 0; i < ast->as.program.count; i++) {
            ASTNode* n = ast->as.program.nodes[i];
            if (n->type != NODE_USE) continue;
            
            if (strcmp(n->as.use_file, "math") == 0 || strcmp(n->as.use_file, "filesystem") == 0) {
                printf("[Tantrums] Imported native standard library '%s'\n", n->as.use_file);
                memmove(&ast->as.program.nodes[i], &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (ast->as.program.count - i - 1));
                ast->as.program.count--;
                ast_free(n);
                i--;
                continue;
            }

            char* resolved = resolve_import_path(source_path, n->as.use_file);
            free(n->as.use_file);
            n->as.use_file = resolved;
            const char* filename = n->as.use_file;

            bool already = false;
            for (int j = 0; j < import_count; j++)
                if (strcmp(imported_files[j], filename) == 0) { already = true; break; }
            if (already) {
                memmove(&ast->as.program.nodes[i], &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (ast->as.program.count - i - 1));
                ast->as.program.count--;
                ast_free(n);
                i--;
                continue;
            }
            if (import_count < 64) {
                imported_files[import_count] = (char*)malloc(strlen(filename) + 1);
                strcpy(imported_files[import_count], filename);
                import_count++;
            }

            FILE* f = fopen(filename, "rb");
            if (!f) {
                fprintf(stderr, "[Line %d] Import Error: Cannot find '%s'.\n", n->line, filename);
                for (int j = 0; j < import_count; j++) free(imported_files[j]);
                ast_free(ast);
                return nullptr;
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* imp_src = (char*)malloc(sz + 1);
            fread(imp_src, 1, sz, f);
            imp_src[sz] = '\0';
            fclose(f);

            CompileMode imp_mode = strip_mode(imp_src);
            MemoryDirectives imp_mem = strip_memory_directives(imp_src);

            Lexer il;
            lexer_init(&il, imp_src);
            TokenList it = lexer_scan_tokens(&il);

            for (int ti = 0; ti < it.count; ti++) {
                if (it.tokens[ti].type == TOKEN_ERROR) {
                    fprintf(stderr, "[Line %d] Error in '%s': %.*s\n",
                            it.tokens[ti].line, filename, it.tokens[ti].length, it.tokens[ti].start);
                    tokenlist_free(&it);
                    free(imp_src);
                    for (int j = 0; j < import_count; j++) free(imported_files[j]);
                    ast_free(ast);
                    return nullptr;
                }
            }

            ASTNode* imp_ast = parser_parse(&it);
            tokenlist_free(&it);
            free(imp_src);

            if (!imp_ast) {
                fprintf(stderr, "[Line %d] Import Error: Failed to parse '%s'.\n", n->line, filename);
                for (int j = 0; j < import_count; j++) free(imported_files[j]);
                ast_free(ast);
                return nullptr;
            }

            for (int k = 0; k < imp_ast->as.program.count; k++) {
                imp_ast->as.program.nodes[k]->node_mode = (int)imp_mode;
                imp_ast->as.program.nodes[k]->node_autofree = imp_mem.autofree;
                imp_ast->as.program.nodes[k]->node_allow_leaks = imp_mem.allow_leaks;
            }

            const char* mode_str = imp_mode == MODE_STATIC ? "static" :
                                   imp_mode == MODE_DYNAMIC ? "dynamic" : "both";
            printf("[Tantrums] Imported '%s' (%d declarations, mode: %s)\n",
                   filename, imp_ast->as.program.count, mode_str);

            int inj = imp_ast->as.program.count;
            if (inj > 0) {
                int old_count = ast->as.program.count;
                int need = old_count - 1 + inj;
                while (ast->as.program.capacity < need) {
                    int cap = ast->as.program.capacity < 8 ? 8 : ast->as.program.capacity * 2;
                    ast->as.program.nodes = (ASTNode**)realloc(ast->as.program.nodes, sizeof(ASTNode*) * cap);
                    ast->as.program.capacity = cap;
                }
                memmove(&ast->as.program.nodes[i + inj],
                        &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (old_count - i - 1));
                for (int k = 0; k < inj; k++) {
                    ast->as.program.nodes[i + k] = imp_ast->as.program.nodes[k];
                    imp_ast->as.program.nodes[k] = nullptr;
                }
                ast->as.program.count = need;
                ast_free(n);
                i += inj - 1;
            } else {
                memmove(&ast->as.program.nodes[i], &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (ast->as.program.count - i - 1));
                ast->as.program.count--;
                ast_free(n);
                i--;
            }
            free(imp_ast->as.program.nodes);
            free(imp_ast);
        }
        for (int j = 0; j < import_count; j++) free(imported_files[j]);
    }

    return ast;
}

/* ── Replace extension ───────────────────────────────── */

static char* make_exe_path(const char* source_path) {
    int len = (int)strlen(source_path);
    int dot = len;
    for (int i = len - 1; i >= 0; i--) {
        if (source_path[i] == '.') { dot = i; break; }
    }
#ifdef _WIN32
    int new_len = dot + 4; /* .exe */
    char* out = (char*)malloc(new_len + 1);
    memcpy(out, source_path, dot);
    memcpy(out + dot, ".exe", 4);
    out[new_len] = '\0';
#else
    char* out = (char*)malloc(dot + 1);
    memcpy(out, source_path, dot);
    out[dot] = '\0';
#endif
    return out;
}

static char* make_obj_path(const char* source_path) {
    int len = (int)strlen(source_path);
    int dot = len;
    for (int i = len - 1; i >= 0; i--) {
        if (source_path[i] == '.') { dot = i; break; }
    }
    int new_len = dot + 4;
    char* out = (char*)malloc(new_len + 1);
    memcpy(out, source_path, dot);
    memcpy(out + dot, ".obj", 4);
    out[new_len] = '\0';
    return out;
}

static char* make_bytecode_path(const char* source_path) {
    int len = (int)strlen(source_path);
    int dot = len;
    for (int i = len - 1; i >= 0; i--) {
        if (source_path[i] == '.') { dot = i; break; }
    }
    int new_len = dot + 6;
    char* out = (char*)malloc(new_len + 1);
    memcpy(out, source_path, dot);
    memcpy(out + dot, ".42ass", 6);
    out[new_len] = '\0';
    return out;
}

static bool check_extension(const char* file_path) {
    const char* ext = strrchr(file_path, '.');
    if (!ext || (strcmp(ext, ".42AHH") != 0 && strcmp(ext, ".trinitrotoluene") != 0)) {
        fprintf(stderr, "Did you really think you can get away with the .42AHH extension just by changing names 🥀🥀🥀\nCompile Error: reference the above note\n");
        return false;
    }
    return true;
}
/* ── Usage ───────────────────────────────────────────── */

static void print_usage() {
    printf("Tantrums %s\n", TANTRUMS_VERSION);
    printf("Usage:\n");
    printf("  tantrums build <file.42AHH | file.trinitrotoluene>    Compile to native executable\n");
    printf("  tantrums run <file.42AHH | file.trinitrotoluene>      Build + run immediately\n");
    printf("  tantrums compile <file.42AHH | file.trinitrotoluene>  Compile to .42ass bytecode\n");
}

/* ── Main ────────────────────────────────────────────── */

int main(int argc, char* argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
#endif
    if (argc < 2) { print_usage(); return 0; }

    /* ── build command ─────────────────────────────── */
    if (strcmp(argv[1], "build") == 0 || strcmp(argv[1], "run") == 0) {
        bool do_run = (strcmp(argv[1], "run") == 0);

        int arg_idx = 2;
        if (argc > 2 && strcmp(argv[arg_idx], "--no-autofree-notes") == 0) {
            suppress_autofree_notes = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Usage: tantrums %s [--no-autofree-notes] <file.42AHH | file.trinitrotoluene>\n", argv[1]);
            return 1;
        }
        const char* file_path = argv[arg_idx];
        if (!check_extension(file_path)) return 1;

        char* source = read_file(file_path);
        if (!source) return 1;

        /* Parse once — AST is reused for both validation and codegen.
         * source buffer MUST stay alive while AST exists because
         * token start pointers reference into it. */
        CompileMode mode;
        ASTNode* ast = prepare_ast(source, file_path, &mode);
        if (!ast) { free(source); return 1; }

        /* Validate via compiler_compile (type errors, escape analysis, etc.)
         * compiler_compile reads AST but does NOT modify or free it.
         * The returned ObjFunction bytecode is discarded — we only care
         * about whether it printed errors. */
        ObjFunction* script = compiler_compile(ast, mode);
        if (!script) {
            fprintf(stderr, "Compilation failed.\n");
            ast_free(ast);
            free(source);
            return 1;
        }

        /* LLVM codegen on the SAME AST */
        char* obj_path = make_obj_path(file_path);
        char* exe_path = make_exe_path(file_path);

        bool ok = llvm_codegen_compile(ast, mode, file_path, obj_path,
                                        global_autofree, global_allow_leaks);
        ast_free(ast);
        /* Source buffer can now be freed since AST is gone */
        free(source);

        if (!ok) {
            fprintf(stderr, "[Tantrums] LLVM compilation failed.\n");
            free(obj_path);
            free(exe_path);
            return 1;
        }

        ok = llvm_codegen_link(obj_path, exe_path);
        if (!ok) {
            free(obj_path);
            free(exe_path);
            return 1;
        }

        printf("[Tantrums] Built: %s\n", exe_path);

        if (do_run) {
            printf("[Tantrums] Running...\n\n");
            char run_cmd[4096];
            snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", exe_path);
            int rc = system(run_cmd);
            free(obj_path);
            free(exe_path);
            return rc;
        }

        free(obj_path);
        free(exe_path);
        return 0;

    } else if (strcmp(argv[1], "compile") == 0) {
        /* Legacy bytecode compilation */
        int arg_idx = 2;
        if (argc > 2 && strcmp(argv[arg_idx], "--no-autofree-notes") == 0) {
            suppress_autofree_notes = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Usage: tantrums compile [--no-autofree-notes] <file.42AHH | file.trinitrotoluene>\n");
            return 1;
        }
        const char* file_path = argv[arg_idx];
        if (!check_extension(file_path)) return 1;

        char* source = read_file(file_path);
        if (!source) return 1;

        CompileMode mode;
        ASTNode* ast = prepare_ast(source, file_path, &mode);
        if (!ast) { free(source); return 1; }

        ObjFunction* script = compiler_compile(ast, mode);
        ast_free(ast);

        if (!script) {
            fprintf(stderr, "Compilation failed.\n");
            free(source);
            return 1;
        }

        char* bytecode_path = make_bytecode_path(file_path);
        bytecode_write(bytecode_path, script);
        printf("Compiled successfully to '%s'.\n", bytecode_path);

        free(bytecode_path);
        free(source);
        return 0;

    } else {
        fprintf(stderr, "Unknown command '%s'. Use build, run, or compile.\n", argv[1]);
        return 1;
    }
}