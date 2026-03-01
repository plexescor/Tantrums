#include "vm.h"
#include "bytecode_file.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char* current_bytecode_path = nullptr;
bool suppress_autofree_notes = false;

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
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/* Replace extension: "file.42AHH" -> "file.42ass" */
static char* make_bytecode_path(const char* source_path) {
    int len = (int)strlen(source_path);
    /* Find last dot */
    int dot = len;
    for (int i = len - 1; i >= 0; i--) {
        if (source_path[i] == '.') { dot = i; break; }
    }
    int new_len = dot + 6; /* .42ass */
    char* out = (char*)malloc(new_len + 1);
    memcpy(out, source_path, dot);
    memcpy(out + dot, ".42ass", 6);
    out[new_len] = '\0';
    return out;
}

/* Get the directory part of a path (including trailing slash), or "" if none */
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

/* Resolve an import path relative to the importing file's directory.
 * If import_path is already absolute, return it as-is.
 * Otherwise prepend the importer's directory. */
static char* resolve_import_path(const char* importer_path, const char* import_path) {
    /* Absolute paths (Unix or Windows) */
    if (import_path[0] == '/' || import_path[0] == '\\' ||
        (import_path[1] == ':')) { /* Windows C:\ style */
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

/* Pre-scan source for #mode, strip it, return detected mode */
static CompileMode strip_mode(char* source) {
    CompileMode mode = MODE_BOTH;
    char* mode_line = source;
    while ((mode_line = strstr(mode_line, "#mode "))) {
        if (strncmp(mode_line + 6, "static", 6) == 0) {
            mode = MODE_STATIC;
        } else if (strncmp(mode_line + 6, "dynamic", 7) == 0) {
            mode = MODE_DYNAMIC;
        } else if (strncmp(mode_line + 6, "both", 4) == 0) {
            mode = MODE_BOTH;
        }
        char* end = strchr(mode_line, '\n');
        if (!end) end = mode_line + strlen(mode_line);
        memset(mode_line, ' ', end - mode_line);
        mode_line = end;
    }
    return mode;
}

/* Per-file memory directive state */
struct MemoryDirectives {
    int autofree;    /* -1 = not declared, 0 = false, 1 = true */
    int allow_leaks; /* -1 = not declared, 0 = false, 1 = true */
};

/* Pre-scan source for #autoFree and #allowMemoryLeaks, strip them, return state.
 * Returns -1 for any directive not explicitly declared in this file. */
static MemoryDirectives strip_memory_directives(char* source) {
    MemoryDirectives d;
    d.autofree    = -1;
    d.allow_leaks = -1;

    char* p = source;
    while ((p = strchr(p, '#')) != nullptr) {
        if (strncmp(p, "#autoFree", 9) == 0) {
            char* after = p + 9;
            while (*after == ' ' || *after == '\t') after++;
            if (strncmp(after, "true", 4) == 0)       d.autofree = 1;
            else if (strncmp(after, "false", 5) == 0)  d.autofree = 0;
            /* Strip the whole line */
            char* end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            memset(p, ' ', end - p);
            p = end;
        } else if (strncmp(p, "#allowMemoryLeaks", 17) == 0) {
            char* after = p + 17;
            while (*after == ' ' || *after == '\t') after++;
            if (strncmp(after, "true", 4) == 0)       d.allow_leaks = 1;
            else if (strncmp(after, "false", 5) == 0)  d.allow_leaks = 0;
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

/* Compile .42AHH source to .42ass bytecode file */
static ObjFunction* compile_source(char* source, const char* source_path) {
    /* ── Pre-scan for #mode directive in main file ── */
    CompileMode mode = strip_mode(source);
    if (mode == MODE_STATIC)  printf("[Tantrums] Mode: static (all variables must have types)\n");
    else if (mode == MODE_DYNAMIC) printf("[Tantrums] Mode: dynamic (no type checking)\n");
    else printf("[Tantrums] Mode: both (typed + dynamic)\n");

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

    /* ── Resolve imports (use statements) ── */
    if (ast->type == NODE_PROGRAM) {
        char* imported_files[64];
        int import_count = 0;

        for (int i = 0; i < ast->as.program.count; i++) {
            ASTNode* n = ast->as.program.nodes[i];
            if (n->type != NODE_USE) continue;

            /* Resolve path relative to the importing file */
            char* resolved = resolve_import_path(source_path, n->as.use_file);

            /* Free old use_file and replace with resolved path */
            free(n->as.use_file);
            n->as.use_file = resolved;
            const char* filename = n->as.use_file;

            /* Skip duplicates */
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

            /* Read imported file */
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

            /* Detect and strip the imported file's own #mode and memory directives */
            CompileMode imp_mode = strip_mode(imp_src);
            MemoryDirectives imp_mem = strip_memory_directives(imp_src);

            /* Lex & parse imported file */
            Lexer il;
            lexer_init(&il, imp_src);
            TokenList it = lexer_scan_tokens(&il);

            for (int ti = 0; ti < it.count; ti++) {
                if (it.tokens[ti].type == TOKEN_ERROR) {
                    fprintf(stderr, "[Line %d] Error in '%s': %.*s\n",
                            it.tokens[ti].line, filename,
                            it.tokens[ti].length, it.tokens[ti].start);
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

            /* Tag every top-level node from this imported file with its own mode and memory directives */
            for (int k = 0; k < imp_ast->as.program.count; k++) {
                imp_ast->as.program.nodes[k]->node_mode        = (int)imp_mode;
                imp_ast->as.program.nodes[k]->node_autofree    = imp_mem.autofree;
                imp_ast->as.program.nodes[k]->node_allow_leaks = imp_mem.allow_leaks;
            }

            const char* mode_str = imp_mode == MODE_STATIC ? "static" :
                                   imp_mode == MODE_DYNAMIC ? "dynamic" : "both";
            printf("[Tantrums] Imported '%s' (%d declarations, mode: %s)\n",
                   filename, imp_ast->as.program.count, mode_str);

            /* Inject imported declarations, replacing the USE node */
            int inj = imp_ast->as.program.count;
            if (inj > 0) {
                int old = ast->as.program.count;
                int need = old - 1 + inj;
                while (ast->as.program.capacity < need) {
                    int cap = ast->as.program.capacity < 8 ? 8 : ast->as.program.capacity * 2;
                    ast->as.program.nodes = (ASTNode**)realloc(ast->as.program.nodes, sizeof(ASTNode*) * cap);
                    ast->as.program.capacity = cap;
                }
                memmove(&ast->as.program.nodes[i + inj],
                        &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (old - i - 1));
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

    ObjFunction* script = compiler_compile(ast, mode);
    ast_free(ast);
    return script;
}

/* Run a compiled ObjFunction in the VM */
static InterpretResult run_script(VM* vm, ObjFunction* script) {
    vm_push(vm, OBJ_VAL(script));
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->function = script;
    frame->ip = script->chunk->code;
    frame->slots = vm->stack;
    return INTERPRET_OK;
}

static void print_usage() {
    printf("Tantrums %s\n", TANTRUMS_VERSION);
    printf("Usage:\n");
    printf("  tantrums run <file.42AHH>      Compile to .42ass and run\n");
    printf("  tantrums compile <file.42AHH>  Compile to .42ass only\n");
    printf("  tantrums exec <file.42ass>     Run an existing .42ass file\n");
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul 2>&1");
#endif
    if (argc < 2) { print_usage(); return 0; }

    const char* file_path = nullptr;

    if (strcmp(argv[1], "run") == 0) {
        int arg_idx = 2;
        if (argc > 2 && strcmp(argv[arg_idx], "--no-autofree-notes") == 0) {
            suppress_autofree_notes = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Usage: tantrums run [--no-autofree-notes] <file.42AHH>\n");
            return 1;
        }
        file_path = argv[arg_idx];

        // printf("DEBUG: read_file\n");
        char* source = read_file(file_path);
        if (!source) return 1;

        // printf("DEBUG: calling compile_source\n");
        ObjFunction* script = compile_source(source, file_path);
        if (!script) {
            fprintf(stderr, "Compilation failed.\n");
            free(source);
            return 1;
        }
        // printf("DEBUG: compiled_source returned\n");

        /* Generate .42ass file path */
        char* bytecode_path = make_bytecode_path(file_path);
        current_bytecode_path = bytecode_path;
        bytecode_write(bytecode_path, script);

        /* Interpret */
        printf("[Tantrums] Compiled -> %s\n", bytecode_path);
        VM* vm = (VM*)malloc(sizeof(VM));
        vm_init(vm);
        InterpretResult result = vm_interpret_compiled(vm, script);
        vm_free(vm);
        free(vm);

        if (bytecode_path) free(bytecode_path);
        free(source);
        current_bytecode_path = nullptr;
        return (result == INTERPRET_OK) ? 0 : 1;
        
    } else if (strcmp(argv[1], "compile") == 0) {
        int arg_idx = 2;
        if (argc > 2 && strcmp(argv[arg_idx], "--no-autofree-notes") == 0) {
            suppress_autofree_notes = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Usage: tantrums compile [--no-autofree-notes] <file.42AHH>\n");
            return 1;
        }
        file_path = argv[arg_idx];

        char* source = read_file(file_path);
        if (!source) return 1;
        ObjFunction* script = compile_source(source, file_path);
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

    } else if (strcmp(argv[1], "exec") == 0) {
        int arg_idx = 2;
        if (argc > 2 && strcmp(argv[arg_idx], "--no-autofree-notes") == 0) {
            suppress_autofree_notes = true;
            arg_idx++;
        }
        if (arg_idx >= argc) {
            fprintf(stderr, "Usage: tantrums exec [--no-autofree-notes] <file.42ass>\n");
            return 1;
        }
        file_path = argv[arg_idx];

        ObjFunction* script = bytecode_read(file_path);
        if (!script) {
            fprintf(stderr, "Failed to load bytecode file.\n");
            return 1;
        }
        VM* vm = (VM*)malloc(sizeof(VM));
        vm_init(vm);
        current_bytecode_path = file_path;
        InterpretResult result = vm_interpret_compiled(vm, script);
        vm_free(vm);
        free(vm);
        current_bytecode_path = nullptr;
        return (result == INTERPRET_OK) ? 0 : 1;

    } else {
        fprintf(stderr, "Unknown command '%s'. Use run, compile, or exec.\n", argv[1]);
        return 1;
    }
}