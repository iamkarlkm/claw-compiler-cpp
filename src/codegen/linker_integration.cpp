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

void claw_print(long long x) {
    printf("%lld", x);
}

void claw_println(long long x) {
    printf("%lld\n", x);
}
)";

LinkerIntegration::LinkerIntegration()
    : runtime_object_path_("/tmp/claw_aot_runtime.o"),
      linker_cmd_(detect_fast_linker()) {}

bool LinkerIntegration::has_system_linker() {
    int ret = std::system("cc --version > /dev/null 2>&1");
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
        "cc -fuse-ld=zld",   // zld - fastest macOS linker
        "cc -fuse-ld=mold",  // mold - fastest Linux linker
        "cc -fuse-ld=lld",   // lld - LLVM linker
    };
    std::string chosen = "cc";
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

bool LinkerIntegration::compile_runtime_stub() {
    if (!runtime_object_path_.empty()) {
        std::ifstream check(runtime_object_path_);
        if (check.good()) return true;
    }

    // Write runtime source to temp file
    const char* tmp_src = "/tmp/claw_aot_runtime.c";
    std::ofstream src(tmp_src);
    if (!src.is_open()) {
        error_ = "Failed to write runtime source file";
        return false;
    }
    src << AOT_RUNTIME_SOURCE;
    src.close();

    // Compile to object
    runtime_object_path_ = "/tmp/claw_aot_runtime.o";
    std::string cmd = "cc -c -O2 ";
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
