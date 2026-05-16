// common/compilation_cache.h - Compilation cache for incremental build speed
// Caches compiled bytecode modules keyed by source hash + compiler config.
// Inspired by MoonBit's millisecond-level incremental compilation.

#ifndef CLAW_COMPILATION_CACHE_H
#define CLAW_COMPILATION_CACHE_H

#include <string>
#include <vector>
#include <memory>
#include "../bytecode/bytecode.h"

namespace claw {

// ============================================================================
// Compilation Cache
// ============================================================================
class CompilationCache {
public:
    CompilationCache();

    // Check if a compiled module is available for the given source and config.
    bool has_cache(const std::string& source,
                   const std::string& filename,
                   const std::string& config_key);

    // Load a cached bytecode module. Call has_cache first.
    std::shared_ptr<bytecode::Module> load_module();

    // Save a compiled bytecode module to cache.
    void save_module(const std::string& source,
                     const std::string& filename,
                     const std::string& config_key,
                     const bytecode::Module& module);

    // Invalidate all cached entries.
    void invalidate_all();

private:
    std::string cache_dir_;
    std::string current_key_;

    std::string compute_key(const std::string& source,
                            const std::string& filename,
                            const std::string& config_key);
    std::string cache_path(const std::string& key);
    bool ensure_cache_dir();
};

} // namespace claw

#endif // CLAW_COMPILATION_CACHE_H
