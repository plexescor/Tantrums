#include "vm.h"
#include "bytecode_file.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
#endif

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

/* Compile .42AHH source to .42ass bytecode file */
static ObjFunction* compile_source(char* source) {
    /* ── Pre-scan for #mode directive ── */
    CompileMode mode = MODE_BOTH;
    char* mode_line = strstr(source, "#mode ");
    if (mode_line) {
        if (strncmp(mode_line + 6, "static", 6) == 0) {
            mode = MODE_STATIC;
            printf("[Tantrums] Mode: static (all variables must have types)\n");
        } else if (strncmp(mode_line + 6, "dynamic", 7) == 0) {
            mode = MODE_DYNAMIC;
            printf("[Tantrums] Mode: dynamic (no type checking)\n");
        } else if (strncmp(mode_line + 6, "both", 4) == 0) {
            mode = MODE_BOTH;
            printf("[Tantrums] Mode: both (typed + dynamic)\n");
        }
        /* Strip the #mode line (preserve line numbers) */
        char* end = strchr(mode_line, '\n');
        if (!end) end = mode_line + strlen(mode_line);
        memset(mode_line, ' ', end - mode_line);
    }

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

            const char* filename = n->as.use_file;

            /* Skip duplicates */
            bool already = false;
            for (int j = 0; j < import_count; j++)
                if (strcmp(imported_files[j], filename) == 0) { already = true; break; }
            if (already) {
                /* Remove the duplicate USE node */
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
                fprintf(stderr, "[Line %d] Import Error: Cannot find '%s' in current directory.\n",
                        n->line, filename);
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

            /* Lex & parse imported file */
            Lexer il;
            lexer_init(&il, imp_src);
            TokenList it = lexer_scan_tokens(&il);
            ASTNode* imp_ast = parser_parse(&it);
            tokenlist_free(&it);
            free(imp_src);

            if (!imp_ast) {
                fprintf(stderr, "[Line %d] Import Error: Failed to parse '%s'.\n", n->line, filename);
                for (int j = 0; j < import_count; j++) free(imported_files[j]);
                ast_free(ast);
                return nullptr;
            }

            printf("[Tantrums] Imported '%s' (%d declarations)\n", filename, imp_ast->as.program.count);

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
                /* Shift remaining nodes to make room */
                memmove(&ast->as.program.nodes[i + inj],
                        &ast->as.program.nodes[i + 1],
                        sizeof(ASTNode*) * (old - i - 1));
                /* Copy imported nodes */
                for (int k = 0; k < inj; k++) {
                    ast->as.program.nodes[i + k] = imp_ast->as.program.nodes[k];
                    imp_ast->as.program.nodes[k] = nullptr;
                }
                ast->as.program.count = need;
                ast_free(n); /* free the USE node */
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
    SetConsoleOutputCP(65001);
#endif
    if (argc < 2) { print_usage(); return 0; }

    const char* cmd = argv[1];

    /* ── tantrums run <file.42AHH> ─────────────────── */
    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: tantrums run <file.42AHH>\n"); return 1; }
        const char* source_path = argv[2];

        /* Step 1: Read source */
        char* source = read_file(source_path);
        if (!source) return 1;

        /* Step 2: Compile to ObjFunction */
        ObjFunction* script = compile_source(source);
        free(source);
        if (!script) { fprintf(stderr, "Compilation failed.\n"); return 65; }

        /* Step 3: Write .42ass bytecode file */
        char* bc_path = make_bytecode_path(source_path);
        if (!bytecode_write(bc_path, script)) { free(bc_path); return 1; }
        printf("[Tantrums] Compiled -> %s\n", bc_path);

        /* Step 4: Load .42ass and run */
        ObjFunction* loaded = bytecode_read(bc_path);
        free(bc_path);
        if (!loaded) return 1;

        VM* vm = (VM*)malloc(sizeof(VM));
        vm_init(vm);
        InterpretResult result = vm_interpret_compiled(vm, loaded);
        vm_free(vm);
        free(vm);

        return result == INTERPRET_OK ? 0 : 70;
    }

    /* ── tantrums compile <file.42AHH> ─────────────── */
    if (strcmp(cmd, "compile") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: tantrums compile <file.42AHH>\n"); return 1; }
        const char* source_path = argv[2];

        char* source = read_file(source_path);
        if (!source) return 1;

        ObjFunction* script = compile_source(source);
        free(source);
        if (!script) { fprintf(stderr, "Compilation failed.\n"); return 65; }

        char* bc_path = make_bytecode_path(source_path);
        bool ok = bytecode_write(bc_path, script);
        if (ok) printf("[Tantrums] Compiled -> %s\n", bc_path);
        free(bc_path);

        return ok ? 0 : 1;
    }

    /* ── tantrums exec <file.42ass> ────────────────── */
    if (strcmp(cmd, "exec") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: tantrums exec <file.42ass>\n"); return 1; }
        const char* bc_path = argv[2];

        ObjFunction* script = bytecode_read(bc_path);
        if (!script) return 1;

        VM* vm = (VM*)malloc(sizeof(VM));
        vm_init(vm);
        InterpretResult result = vm_interpret_compiled(vm, script);
        vm_free(vm);
        free(vm);

        return result == INTERPRET_OK ? 0 : 70;
    }

    fprintf(stderr, "Unknown command '%s'. Use 'run', 'compile', or 'exec'.\n", cmd);
    return 1;
}
