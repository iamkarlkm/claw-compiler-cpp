// codegen/linker_integration.h - System linker integration for AOT compilation

#ifndef CLAW_LINKER_INTEGRATION_H
#define CLAW_LINKER_INTEGRATION_H

#include <string>
#include <vector>

namespace claw {
namespace codegen {

// ============================================================================
// Linker Integration - Wraps system cc/ld for AOT executable generation
// ============================================================================

class LinkerIntegration {
public:
    LinkerIntegration();

    // Link object file(s) into an executable
    // object_files: list of .o files to link
    // output_file: path to output executable
    // extra_libs: additional libraries to link against
    bool link_executable(const std::vector<std::string>& object_files,
                         const std::string& output_file,
                         const std::vector<std::string>& extra_libs = {});

    // Link with the AOT runtime stub automatically
    bool link_with_runtime(const std::string& object_file,
                           const std::string& output_file);

    // Get the last error message
    const std::string& get_error() const { return error_; }

    // Check if the system linker (cc) is available
    static bool has_system_linker();

private:
    std::string error_;
    std::string runtime_object_path_; // cached path to compiled runtime stub

    bool compile_runtime_stub();
    bool invoke_linker(const std::vector<std::string>& args);

    // Detect the fastest available linker and return the cc invocation prefix
    std::string detect_fast_linker();

    std::string linker_cmd_;  // e.g. "cc" or "cc -fuse-ld=zld"
};

} // namespace codegen
} // namespace claw

#endif // CLAW_LINKER_INTEGRATION_H
