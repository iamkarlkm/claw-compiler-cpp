// common/parse_cache.h - Token parse cache for incremental compilation speed

#ifndef CLAW_PARSE_CACHE_H
#define CLAW_PARSE_CACHE_H

#include <string>
#include <vector>
#include "lexer/token.h"

namespace claw {

class ParseCache {
public:
    ParseCache();

    // Check if cached tokens are available and valid for the given source.
    bool has_cache(const std::string& source, const std::string& filename = "");

    // Load tokens from cache. Call has_cache first.
    std::vector<Token> load_tokens();

    // Save tokens to cache keyed by source content.
    void save_tokens(const std::string& source,
                     const std::string& filename,
                     const std::vector<Token>& tokens);

    // Invalidate all cached entries.
    void invalidate_all();

private:
    std::string cache_dir_;
    std::string current_key_;

    std::string compute_key(const std::string& source,
                            const std::string& filename);
    std::string cache_path(const std::string& key);
    bool ensure_cache_dir();
};

} // namespace claw

#endif // CLAW_PARSE_CACHE_H
