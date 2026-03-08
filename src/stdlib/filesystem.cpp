#include "stdlib/filesystem.h"
#include "value.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════════
//  Internal helpers (identical to maths.cpp pattern)
// ══════════════════════════════════════════════════════════════════

#ifdef TANTRUMS_RUNTIME_OBJ
// The compiler executable (tantrums.exe) links this file but does NOT link runtime.cpp.
// Provide dummy implementations for the linker. These are never called by the compiler natively.
extern "C" {
    TantrumsValue rt_string_from_cstr(const char* s) { return TV_NULL; }
    void rt_throw(TantrumsValue val) { }
}
#endif

static Value tv_to_value_fs(TantrumsValue tv) {
    int tag = tv_tag(tv);
    if (tag == TV_TAG_INT)   return INT_VAL(tv_to_int(tv));
    if (tag == TV_TAG_FLOAT) return FLOAT_VAL(tv_to_float(tv));
    if (tag == TV_TAG_BOOL)  return BOOL_VAL(tv_to_bool(tv));
    if (tag == TV_TAG_NULL)  return NULL_VAL;
    if (tag == TV_TAG_OBJ)   return OBJ_VAL((Obj*)tv_to_obj(tv));
    return NULL_VAL;
}

static TantrumsValue value_to_tv_fs(Value v) {
    if (IS_INT(v))   return tv_int(AS_INT(v));
    if (IS_FLOAT(v)) return tv_float(AS_FLOAT(v));
    if (IS_BOOL(v))  return tv_bool(AS_BOOL(v));
    if (IS_NULL(v))  return TV_NULL;
    if (IS_OBJ(v))   return tv_obj((void*)AS_OBJ(v));
    return TV_NULL;
}

static std::string resolve_path(const std::string& path) {
    std::string res = path;
    size_t pos = res.find("${USERHOME}");
    if (pos != std::string::npos) {
#ifdef _WIN32
        const char* home = getenv("USERPROFILE");
#else
        const char* home = getenv("HOME");
#endif
        if (!home) {
            rt_throw(rt_string_from_cstr("${USERHOME} could not be resolved on this system"));
        }
        res.replace(pos, 11, home);
    }
    return res;
}

static std::string get_string_arg(TantrumsValue tv, const char* func_name, const char* arg_name) {
    Value v = tv_to_value_fs(tv);
    if (!IS_STRING(v)) {
        std::string err = std::string(func_name) + " requires a string argument for " + arg_name;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return std::string(AS_CSTRING(v));
}

static std::string get_exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(NULL, buf, MAX_PATH) == 0) {
        rt_throw(rt_string_from_cstr("resolve_exe_relative: could not determine executable path"));
    }
    return fs::path(buf).parent_path().string();
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        rt_throw(rt_string_from_cstr("resolve_exe_relative: could not determine executable path"));
    }
    return fs::path(buf).parent_path().string();
#elif defined(__linux__)
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) {
        rt_throw(rt_string_from_cstr("resolve_exe_relative: could not determine executable path"));
    }
    buf[len] = '\0';
    return fs::path(buf).parent_path().string();
#else
    rt_throw(rt_string_from_cstr("resolve_exe_relative: unsupported platform"));
    return "";
#endif
}

static std::string resolve_exe_relative(const std::string& path) {
    if (path.empty()) return path;

    bool starts_with_slash = (path[0] == '/' || path[0] == '\\');
    bool has_drive = (path.size() >= 2 && path[1] == ':');

    if (!starts_with_slash && !has_drive) {
        fs::path exe_dir = get_exe_dir();
        return (exe_dir / path).string();
    }
    
    return path;
}

// ══════════════════════════════════════════════════════════════════
//  Extern C Interface
// ══════════════════════════════════════════════════════════════════

extern "C" {

TantrumsValue rt_filesystem_read(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.read", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);

    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        std::string err = "filesystem.read: File not found or is a directory: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        std::string err = "filesystem.read: Could not open file: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string buffer(size, ' ');
    file.seekg(0);
    file.read(&buffer[0], size);

    return rt_string_from_cstr(buffer.c_str());
}

TantrumsValue rt_filesystem_write(TantrumsValue path_tv, TantrumsValue data_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.write", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::string data = get_string_arg(data_tv, "filesystem.write", "data");

    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file) {
        std::string err = "filesystem.write: Could not write to file: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    file.write(data.c_str(), data.size());
    return tv_bool(true);
}

TantrumsValue rt_filesystem_append(TantrumsValue path_tv, TantrumsValue data_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.append", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::string data = get_string_arg(data_tv, "filesystem.append", "data");

    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file) {
        std::string err = "filesystem.append: Could not open file for appending: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    file.write(data.c_str(), data.size());
    return tv_bool(true);
}

TantrumsValue rt_filesystem_exists(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.exists", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    return tv_bool(fs::exists(path, ec));
}

TantrumsValue rt_filesystem_delete(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.delete", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return tv_bool(true); // Already gone
    }
    if (fs::is_directory(path, ec)) {
        if (!fs::remove_all(path, ec)) {
            std::string err = "filesystem.delete: Could not delete directory: " + raw_path;
            rt_throw(rt_string_from_cstr(err.c_str()));
        }
    } else {
        if (!fs::remove(path, ec)) {
            std::string err = "filesystem.delete: Could not delete file: " + raw_path;
            rt_throw(rt_string_from_cstr(err.c_str()));
        }
    }
    return tv_bool(true);
}

TantrumsValue rt_filesystem_mkdir(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.mkdir", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    if (!fs::create_directories(path, ec) && ec.value() != 0) {
        std::string err = "filesystem.mkdir: Failed to create directories: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return tv_bool(true);
}

TantrumsValue rt_filesystem_mkfile(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.mkfile", "path");
    std::string resolved = resolve_path(raw_path);
    fs::path path = resolve_exe_relative(resolved);
    std::error_code ec;

    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    
    std::ofstream file(path.string(), std::ios::out | std::ios::binary);
    if (!file) {
        std::string err = "filesystem.mkfile: Could not create file: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return tv_bool(true);
}

TantrumsValue rt_filesystem_listdir(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.listdir", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);

    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
        std::string err = "filesystem.listdir: Not a valid directory: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    ObjList* list = obj_list_new();
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        obj_list_append(list, OBJ_VAL((Obj*)obj_string_new(entry.path().filename().string().c_str(), entry.path().filename().string().size())));
    }
    
    if (ec) {
        std::string err = "filesystem.listdir: Error reading directory: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    return tv_obj(list);
}

TantrumsValue rt_filesystem_isfile(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.isfile", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    return tv_bool(fs::is_regular_file(path, ec));
}

TantrumsValue rt_filesystem_isdir(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.isdir", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    return tv_bool(fs::is_directory(path, ec));
}

TantrumsValue rt_filesystem_copy(TantrumsValue src_tv, TantrumsValue dst_tv) {
    std::string raw_src = get_string_arg(src_tv, "filesystem.copy", "src");
    std::string raw_dst = get_string_arg(dst_tv, "filesystem.copy", "dst");
    std::string src_resolved = resolve_path(raw_src);
    std::string dst_resolved = resolve_path(raw_dst);
    std::string src = resolve_exe_relative(src_resolved);
    fs::path dst = resolve_exe_relative(dst_resolved);
    std::error_code ec;

    if (dst.has_parent_path()) {
        fs::create_directories(dst.parent_path(), ec);
    }

    fs::copy_options opts = fs::copy_options::overwrite_existing | fs::copy_options::recursive;
    fs::copy(src, dst, opts, ec);
    if (ec) {
        std::string err = "filesystem.copy: Could not copy " + raw_src + " to " + raw_dst;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return tv_bool(true);
}

TantrumsValue rt_filesystem_move(TantrumsValue src_tv, TantrumsValue dst_tv) {
    std::string raw_src = get_string_arg(src_tv, "filesystem.move", "src");
    std::string raw_dst = get_string_arg(dst_tv, "filesystem.move", "dst");
    std::string src_resolved = resolve_path(raw_src);
    std::string dst_resolved = resolve_path(raw_dst);
    std::string src = resolve_exe_relative(src_resolved);
    fs::path dst = resolve_exe_relative(dst_resolved);
    std::error_code ec;

    if (dst.has_parent_path()) {
        fs::create_directories(dst.parent_path(), ec);
    }

    fs::rename(src, dst, ec);
    if (ec) {
        std::string err = "filesystem.move: Could not move " + raw_src + " to " + raw_dst;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return tv_bool(true);
}

TantrumsValue rt_filesystem_size(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.size", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        std::string err = "filesystem.size: Could not get size of: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return tv_int(size);
}

TantrumsValue rt_filesystem_readlines(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.readlines", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);

    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        std::string err = "filesystem.readlines: File not found or is a directory: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    std::ifstream file(path);
    if (!file) {
        std::string err = "filesystem.readlines: Could not open file: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }

    ObjList* list = obj_list_new();
    std::string line;
    while (std::getline(file, line)) {
        obj_list_append(list, OBJ_VAL((Obj*)obj_string_new(line.c_str(), line.size())));
    }
    
    return tv_obj(list);
}

TantrumsValue rt_filesystem_writelines(TantrumsValue path_tv, TantrumsValue lines_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.writelines", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    
    Value lines_val = tv_to_value_fs(lines_tv);
    if (!IS_LIST(lines_val)) {
        rt_throw(rt_string_from_cstr("filesystem.writelines: requires a list argument for lines"));
    }
    
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file) {
        std::string err = "filesystem.writelines: Could not write to file: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    
    ObjList* list = AS_LIST(lines_val);
    for (int i = 0; i < list->count; i++) {
        Value item = list->items[i];
        if (!IS_STRING(item)) {
            rt_throw(rt_string_from_cstr("filesystem.writelines: list must contain only strings"));
        }
        std::string line_str = std::string(AS_CSTRING(item));
        file.write(line_str.c_str(), line_str.size());
        file.write("\n", 1);
    }
    
    return tv_bool(true);
}

TantrumsValue rt_filesystem_cwd() {
    std::error_code ec;
    std::string path = fs::current_path(ec).string();
    if (ec) {
        rt_throw(rt_string_from_cstr("filesystem.cwd: Could not get current directory"));
    }
    return rt_string_from_cstr(path.c_str());
}

TantrumsValue rt_filesystem_abspath(TantrumsValue path_tv) {
    std::string raw_path = get_string_arg(path_tv, "filesystem.abspath", "path");
    std::string resolved = resolve_path(raw_path);
    std::string path = resolve_exe_relative(resolved);
    std::error_code ec;
    std::string abs_path = fs::absolute(path, ec).string();
    if (ec) {
        std::string err = "filesystem.abspath: Could not resolve absolute path for: " + raw_path;
        rt_throw(rt_string_from_cstr(err.c_str()));
    }
    return rt_string_from_cstr(abs_path.c_str());
}

} // extern "C"
