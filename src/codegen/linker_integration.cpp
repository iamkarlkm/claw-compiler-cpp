// codegen/linker_integration.cpp - System linker integration

#include "linker_integration.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace claw {
namespace codegen {

// Minimal AOT runtime stub source (embedded to avoid file dependency issues)
static const char* AOT_RUNTIME_SOURCE = R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLAW_OBJ_MAX_FIELDS 32
#define CLAW_OBJ_MAX_NAME   64

typedef struct {
    char name[CLAW_OBJ_MAX_NAME];
    long long value;
} ClawField;

typedef struct {
    char type_name[CLAW_OBJ_MAX_NAME];
    ClawField fields[CLAW_OBJ_MAX_FIELDS];
    int count;
} ClawObject;

extern "C" {

void claw_print(long long x) {
    printf("%lld", x);
}

void claw_println(long long x) {
    printf("%lld\n", x);
}

void* claw_alloc_obj() {
    ClawObject* obj = (ClawObject*)malloc(sizeof(ClawObject));
    if (obj) {
        obj->type_name[0] = '\0';
        obj->count = 0;
    }
    return obj;
}

void* claw_alloc_obj_type(const char* type_name) {
    ClawObject* obj = (ClawObject*)claw_alloc_obj();
    if (obj && type_name) {
        strncpy(obj->type_name, type_name, CLAW_OBJ_MAX_NAME - 1);
        obj->type_name[CLAW_OBJ_MAX_NAME - 1] = '\0';
    }
    return obj;
}

long long claw_load_field(const char* field_name, void* obj_ptr) {
    ClawObject* obj = (ClawObject*)obj_ptr;
    if (!obj || !field_name) return 0;
    for (int i = 0; i < obj->count; i++) {
        if (strcmp(obj->fields[i].name, field_name) == 0) {
            return obj->fields[i].value;
        }
    }
    return 0;
}

void* claw_store_field(const char* field_name, long long value, void* obj_ptr) {
    ClawObject* obj = (ClawObject*)obj_ptr;
    if (!obj || !field_name) return obj_ptr;
    for (int i = 0; i < obj->count; i++) {
        if (strcmp(obj->fields[i].name, field_name) == 0) {
            obj->fields[i].value = value;
            return obj_ptr;
        }
    }
    if (obj->count < CLAW_OBJ_MAX_FIELDS) {
        strncpy(obj->fields[obj->count].name, field_name, CLAW_OBJ_MAX_NAME - 1);
        obj->fields[obj->count].name[CLAW_OBJ_MAX_NAME - 1] = '\0';
        obj->fields[obj->count].value = value;
        obj->count++;
    }
    return obj_ptr;
}

// Array helpers (simplified: void** with nullptr terminator)
long long claw_arr_len(void* arr) {
    if (!arr) return 0;
    void** a = (void**)arr;
    long long len = 0;
    while (a[len]) len++;
    return len;
}

void* claw_arr_push(void* arr, void* val) {
    // Simplified stub: AOT array push not fully implemented
    (void)arr; (void)val;
    return arr;
}

void* claw_arr_range(long long start, long long end, long long step) {
    if (step == 0) step = 1;
    long long count = 0;
    if (step > 0) {
        count = (end > start) ? ((end - start + step - 1) / step) : 0;
    } else {
        count = (end < start) ? ((start - end + (-step) - 1) / (-step)) : 0;
    }
    void** result = (void**)malloc((count + 1) * sizeof(void*));
    long long val = start;
    for (long long i = 0; i < count; i++) {
        result[i] = (void*)val;
        val += step;
    }
    result[count] = nullptr;
    return result;
}

} // extern "C"
)";

LinkerIntegration::LinkerIntegration()
    : runtime_object_path_("/tmp/claw_aot_runtime.o"),
      linker_cmd_(detect_fast_linker()) {}

bool LinkerIntegration::has_system_linker() {
    int ret = std::system("c++ --version > /dev/null 2>&1");
    return ret == 0;
}

static std::string get_cache_dir() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claw/cache";
}

std::string LinkerIntegration::detect_fast_linker() {
    // Prefer faster linkers: zld (macOS) > mold (Linux) > lld > system ld
    // We probe by actually linking a tiny program because --version is
    // handled by the compiler driver, not the linker.
    // The result is cached to a file so we only probe once per system.
    std::string cache_file = get_cache_dir() + "/linker";
    {
        std::ifstream in(cache_file);
        if (in) {
            std::string cached;
            std::getline(in, cached);
            if (!cached.empty()) return cached;
        }
    }

    static const char* candidates[] = {
        "c++ -fuse-ld=zld",   // zld - fastest macOS linker
        "c++ -fuse-ld=mold",  // mold - fastest Linux linker
        "c++ -fuse-ld=lld",   // lld - LLVM linker
    };
    std::string chosen = "c++";
    for (const char* candidate : candidates) {
        std::string probe = "echo 'int main(){}' | " + std::string(candidate) +
                            " -x c - -o /tmp/_claw_linker_probe > /dev/null 2>&1";
        if (std::system(probe.c_str()) == 0) {
            std::remove("/tmp/_claw_linker_probe");
            chosen = candidate;
            break;
        }
    }

    // Write cache
    std::string cmd = "mkdir -p " + get_cache_dir();
    std::system(cmd.c_str());
    std::ofstream out(cache_file);
    if (out) out << chosen;

    return chosen;
}

void LinkerIntegration::set_string_constants(const std::vector<std::string>& strings) {
    string_constants_ = strings;
}

static std::string escape_c_string(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

bool LinkerIntegration::compile_runtime_stub() {
    // Remove any stale cached runtime object so string constants are recompiled
    if (!runtime_object_path_.empty()) {
        std::remove(runtime_object_path_.c_str());
    }

    // Write runtime source to temp file
    const char* tmp_src = "/tmp/claw_aot_runtime.cpp";
    std::ofstream src(tmp_src);
    if (!src.is_open()) {
        error_ = "Failed to write runtime source file";
        return false;
    }
    src << AOT_RUNTIME_SOURCE;

    // Append string constant accessors for AOT
    if (!string_constants_.empty()) {
        src << "\n// String constant accessors\nextern \"C\" {\n";
        for (size_t i = 0; i < string_constants_.size(); ++i) {
            src << "const char* claw_str_" << i << "() { return \"";
            src << escape_c_string(string_constants_[i]);
            src << "\"; }\n";
        }
        src << "}\n";
    }

    src.close();

    // Compile to object
    runtime_object_path_ = "/tmp/claw_aot_runtime.o";
    std::string cmd = "c++ -c -O2 ";
#ifdef __APPLE__
    cmd += "-mmacosx-version-min=10.15 ";
#endif
    cmd += tmp_src;
    cmd += " -o ";
    cmd += runtime_object_path_;

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        error_ = "Failed to compile AOT runtime stub";
        return false;
    }
    return true;
}

bool LinkerIntegration::invoke_linker(const std::vector<std::string>& args) {
    std::string cmd = linker_cmd_;
    for (const auto& arg : args) {
        cmd += " ";
        cmd += arg;
    }

    if (std::getenv("CLAW_VERBOSE_LINK")) {
        std::cout << "[linker] " << cmd << "\n";
    }

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        error_ = "Linker command failed: " + cmd;
        return false;
    }
    return true;
}

bool LinkerIntegration::link_executable(const std::vector<std::string>& object_files,
                                         const std::string& output_file,
                                         const std::vector<std::string>& extra_libs) {
    std::vector<std::string> args;
    for (const auto& obj : object_files) {
        args.push_back(obj);
    }
    for (const auto& lib : extra_libs) {
        args.push_back(lib);
    }
    args.push_back("-o");
    args.push_back(output_file);

    return invoke_linker(args);
}

bool LinkerIntegration::link_with_runtime(const std::string& object_file,
                                           const std::string& output_file) {
    if (!compile_runtime_stub()) {
        return false;
    }

    std::vector<std::string> args;
    args.push_back(object_file);
    args.push_back(runtime_object_path_);
    args.push_back("-o");
    args.push_back(output_file);

    return invoke_linker(args);
}

} // namespace codegen
} // namespace claw
