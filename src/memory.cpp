#include "memory.h"
#include "value.h"
#include "vm.h"
#include "table.h"
#include "chunk.h"
#include <cstdlib>
#include <cstdio>

size_t tantrums_bytes_allocated = 0;
size_t tantrums_peak_bytes_allocated = 0;
size_t tantrums_next_gc = 1024 * 1024;

extern Obj* all_objects;
extern VM* current_vm_for_gc;

void* tantrums_realloc(void* ptr, size_t old_size, size_t new_size) {
    tantrums_bytes_allocated += new_size;
    tantrums_bytes_allocated -= old_size;
    
    if (tantrums_bytes_allocated > tantrums_peak_bytes_allocated) {
        tantrums_peak_bytes_allocated = tantrums_bytes_allocated;
    }

    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
}



void tantrums_gc_collect(void) {
    // GC Disabled. We only clean up at program exit.
}

extern const char* current_bytecode_path;

static void format_with_commas(size_t value, char* buf, size_t buf_size) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%zu", value);
    int len = (int)strlen(temp);
    
    int out_idx = 0;
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            if (out_idx < buf_size - 1) buf[out_idx++] = ',';
        }
        if (out_idx < buf_size - 1) buf[out_idx++] = temp[i];
    }
    buf[out_idx] = '\0';
}

void tantrums_free_all_objects(void) {
    int leak_count = 0;
    size_t total_leaked_bytes = 0;
    
    Obj* obj_iter = all_objects;
    while (obj_iter) {
        if (obj_iter->type == OBJ_POINTER && obj_iter->is_manual) {
            ObjPointer* p = (ObjPointer*)obj_iter;
            if (p->is_valid) {
                leak_count++;
                total_leaked_bytes += p->alloc_size;
            }
        }
        obj_iter = obj_iter->next;
    }

    if (leak_count > 0) {
        FILE* out_file = stderr;
        bool write_to_file = leak_count > 5;
        
        if (write_to_file) {
            fprintf(stderr, "\n[Tantrums Warning] Memory leak detected: %d allocation(s) not freed. See memleaklog.txt in the same directory as the executing bytecode.\n", leak_count);
            
            char log_path[1024] = "memleaklog.txt";
            if (current_bytecode_path) {
                const char* last_slash = strrchr(current_bytecode_path, '/');
                const char* last_bslash = strrchr(current_bytecode_path, '\\');
                const char* dir_end = last_slash > last_bslash ? last_slash : last_bslash;
                if (dir_end) {
                    size_t dir_len = dir_end - current_bytecode_path + 1;
                    if (dir_len < sizeof(log_path)) {
                        strncpy(log_path, current_bytecode_path, dir_len);
                        log_path[dir_len] = '\0';
                        strncat(log_path, "memleaklog.txt", sizeof(log_path) - dir_len - 1);
                    }
                }
            }
            out_file = fopen(log_path, "w");
            if (!out_file) {
                out_file = stderr; // fallback
                fprintf(stderr, "Failed to open memleaklog.txt for writing.\n");
            }
        } else {
            fprintf(stderr, "Memory leak detected (suppressed details, check output if running directly).\n");
        }
        
        char count_str[64];
        format_with_commas(leak_count, count_str, sizeof(count_str));
        
        const char* exec_name = "unknown";
        if (current_bytecode_path) {
            const char* last_slash = strrchr(current_bytecode_path, '/');
            const char* last_bslash = strrchr(current_bytecode_path, '\\');
            const char* start = last_slash > last_bslash ? last_slash : last_bslash;
            exec_name = start ? start + 1 : current_bytecode_path;
        }

        fprintf(out_file, "\nTANTRUMS MEMORY LEAK REPORT\n");
        fprintf(out_file, "============================\n");
        fprintf(out_file, "Executable: %s\n", exec_name);
        fprintf(out_file, "Leaks: %s allocations\n", count_str);
        fprintf(out_file, "============================\n");
        
        int groups_printed = 0;
        int current_group_count = 0;
        const char* prev_func = nullptr;
        const char* prev_type = nullptr;
        int prev_line = -1;
        size_t prev_size = 0;
        
        obj_iter = all_objects;
        while (obj_iter) {
            if (obj_iter->type == OBJ_POINTER && obj_iter->is_manual) {
                ObjPointer* p = (ObjPointer*)obj_iter;
                if (p->is_valid) {
                    const char* func_name = p->alloc_func ? p->alloc_func->chars : "main";
                    const char* type_name = p->alloc_type ? p->alloc_type->chars : "dynamic";
                    int line = p->alloc_line;
                    size_t size = p->alloc_size;
                    
                    if (current_group_count == 0) {
                        prev_func = func_name;
                        prev_type = type_name;
                        prev_line = line;
                        prev_size = size;
                        current_group_count = 1;
                    } else if (prev_line == line && prev_size == size && strcmp(prev_func, func_name) == 0 && strcmp(prev_type, type_name) == 0) {
                        current_group_count++;
                    } else {
                        if (groups_printed < 100) {
                            if (current_group_count > 1) {
                                fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes) [x%d]\n",
                                        prev_line, prev_func, prev_type, prev_size, current_group_count);
                            } else {
                                fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes)\n",
                                        prev_line, prev_func, prev_type, prev_size);
                            }
                            groups_printed++;
                        } else if (groups_printed == 100) {
                            fprintf(out_file, "  ... and more\n");
                            groups_printed++;
                        }
                        
                        prev_func = func_name;
                        prev_type = type_name;
                        prev_line = line;
                        prev_size = size;
                        current_group_count = 1;
                    }
                }
            }
            obj_iter = obj_iter->next;
        }
        
        if (current_group_count > 0) {
            if (groups_printed < 100) {
                if (current_group_count > 1) {
                    fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes) [x%d]\n",
                            prev_line, prev_func, prev_type, prev_size, current_group_count);
                } else {
                    fprintf(out_file, "  alloc at line %d in %s — %s (%zu bytes)\n",
                            prev_line, prev_func, prev_type, prev_size);
                }
            } else if (groups_printed == 100) {
                fprintf(out_file, "  ... and more\n");
            }
        }
        
        char formatted_bytes[64];
        format_with_commas(total_leaked_bytes, formatted_bytes, sizeof(formatted_bytes));
        
        fprintf(out_file, "============================\n");
        fprintf(out_file, "SUMMARY\n");
        fprintf(out_file, "  Total leaked: %s bytes\n", formatted_bytes);
        if (total_leaked_bytes >= 1024) {
            fprintf(out_file, "              = %.2f KB\n", (double)total_leaked_bytes / 1024.0);
        }
        if (total_leaked_bytes >= 1024 * 1024) {
            fprintf(out_file, "              = %.2f MB\n", (double)total_leaked_bytes / (1024.0 * 1024.0));
        }
        if (total_leaked_bytes >= 1024 * 1024 * 1024) {
            fprintf(out_file, "              = %.2f GB\n", (double)total_leaked_bytes / (1024.0 * 1024.0 * 1024.0));
        }
        fprintf(out_file, "============================\n");
        
        if (out_file != stderr) {
            fclose(out_file);
        } else {
            fprintf(stderr, "\n");
        }
    }

    Obj* obj = all_objects;
    while (obj) {
        Obj* next = obj->next;
        obj_free(obj);
        obj = next;
    }
    all_objects = nullptr;
    tantrums_bytes_allocated = 0;
}
