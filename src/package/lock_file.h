// Claw Compiler - Lock File
// Ensures reproducible builds by pinning exact dependency versions

#ifndef CLAW_LOCK_FILE_H
#define CLAW_LOCK_FILE_H

#include "package/manifest_parser.h"
#include "package/dependency_resolver.h"
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace claw {
namespace package {

// ============================================================================
// Locked Package Entry
// ============================================================================

struct LockedPackage {
    std::string name;
    SemVer version;
    std::string source;                    // "registry", "path", "git"
    std::string checksum;                  // SHA-256 of package contents
    std::filesystem::path path;            // Local cache path
    std::string registry_url;              // Source registry URL
    std::vector<std::string> dependencies; // Transitive dependency names
    std::vector<std::string> features;     // Enabled features

    std::string to_string() const;
    static std::optional<LockedPackage> from_string(const std::string& str);
};

// ============================================================================
// Lock File
// ============================================================================

class LockFile {
public:
    LockFile();
    explicit LockFile(const std::filesystem::path& path);

    // Load from file
    bool load(const std::filesystem::path& path);

    // Load from string
    bool load_from_string(const std::string& content);

    // Save to file
    bool save(const std::filesystem::path& path) const;

    // Get lockfile version
    std::string get_version() const { return version_; }

    // Package operations
    bool has_package(const std::string& name) const;
    std::optional<LockedPackage> get_package(const std::string& name) const;
    void add_package(const LockedPackage& pkg);
    void remove_package(const std::string& name);
    void update_package(const LockedPackage& pkg);

    // Get all packages
    const std::vector<LockedPackage>& get_packages() const { return packages_; }
    std::vector<LockedPackage>& get_packages() { return packages_; }

    // Get packages in dependency order
    std::vector<LockedPackage> get_ordered_packages() const;

    // Validate against manifest
    // Returns true if lockfile is still valid for given manifest
    bool is_valid_for_manifest(const Manifest& manifest) const;

    // Generate from resolved graph
    static LockFile from_resolved_graph(const ResolvedGraph& graph);

    // Get metadata
    std::string get_metadata(const std::string& key) const;
    void set_metadata(const std::string& key, const std::string& value);

    // Get last error
    const std::string& get_last_error() const { return last_error_; }

    // Check if lockfile is newer than manifest
    bool is_newer_than(const std::filesystem::path& manifest_path) const;

    // Clear all packages
    void clear();

    // Get checksum for a package (placeholder for actual SHA-256)
    static std::string compute_checksum(const std::filesystem::path& package_path);

    // Serialization (for testing)
    std::string serialize() const;

private:
    std::string version_ = "1";
    std::vector<LockedPackage> packages_;
    std::unordered_map<std::string, size_t> package_index_;  // name -> index
    std::unordered_map<std::string, std::string> metadata_;
    std::filesystem::path source_path_;
    std::string last_error_;

    void rebuild_index();
    bool deserialize(const std::string& content);
};

// ============================================================================
// LockFileManager
// ============================================================================

class LockFileManager {
public:
    // Get default lockfile path for a manifest
    static std::filesystem::path get_lockfile_path(const std::filesystem::path& manifest_path);

    // Check if lockfile exists
    static bool lockfile_exists(const std::filesystem::path& manifest_path);

    // Load or create lockfile
    static LockFile load_or_create(const std::filesystem::path& manifest_path);

    // Update lockfile from resolution result
    static bool update_lockfile(const std::filesystem::path& manifest_path,
                                 const ResolutionResult& result);

    // Remove stale entries (packages no longer in manifest)
    static bool prune_lockfile(const std::filesystem::path& manifest_path,
                                const Manifest& manifest);
};

} // namespace package
} // namespace claw

#endif // CLAW_LOCK_FILE_H
