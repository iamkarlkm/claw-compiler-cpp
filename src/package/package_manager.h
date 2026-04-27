// Claw Compiler - Package Manager
// Central orchestrator for dependency management, installation, and package operations

#ifndef CLAW_PACKAGE_MANAGER_H
#define CLAW_PACKAGE_MANAGER_H

#include "package/manifest_parser.h"
#include "package/dependency_resolver.h"
#include "package/lock_file.h"
#include <functional>
#include <future>

namespace claw {
namespace package {

// ============================================================================
// Package Installation Status
// ============================================================================

enum class InstallStatus {
    Pending,        // Waiting to install
    Downloading,    // Downloading package
    Extracting,     // Extracting package
    Building,       // Building package
    Installed,      // Successfully installed
    Failed,         // Installation failed
    Cached,         // Already in cache
    Skipped         // Skipped (optional or already satisfied)
};

struct InstallRecord {
    std::string package_name;
    SemVer version;
    InstallStatus status = InstallStatus::Pending;
    std::string error_message;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;

    double duration_ms() const {
        auto dur = end_time - start_time;
        return std::chrono::duration<double, std::milli>(dur).count();
    }
};

// ============================================================================
// Package Cache
// ============================================================================

class PackageCache {
public:
    explicit PackageCache(const std::filesystem::path& cache_dir);

    // Get cache directory
    std::filesystem::path get_cache_dir() const { return cache_dir_; }

    // Get package cache path
    std::filesystem::path get_package_path(const std::string& name, const SemVer& version) const;

    // Check if package is cached
    bool is_cached(const std::string& name, const SemVer& version) const;

    // Add package to cache
    bool cache_package(const std::string& name, const SemVer& version,
                       const std::filesystem::path& source_path);

    // Remove package from cache
    bool remove_from_cache(const std::string& name, const SemVer& version);

    // Clean old versions (keep only N newest)
    size_t clean_old_versions(const std::string& name, size_t keep_count = 3);

    // Get all cached versions
    std::vector<SemVer> get_cached_versions(const std::string& name) const;

    // Get cache size in bytes
    size_t get_cache_size() const;

    // Clear entire cache
    bool clear_cache();

private:
    std::filesystem::path cache_dir_;
};

// ============================================================================
// Package Manager
// ============================================================================

enum class PackageCommand {
    Install,        // Install dependencies
    Update,         // Update dependencies
    Remove,         // Remove a dependency
    Add,            // Add a dependency
    Search,         // Search for packages
    Publish,        // Publish package to registry
    Clean,          // Clean cache
    List,           // List installed packages
    Verify,         // Verify lockfile consistency
    Audit,          // Security audit
};

struct PackageManagerConfig {
    std::filesystem::path cache_dir;
    std::filesystem::path registry_url;
    bool offline_mode = false;
    bool verbose = false;
    bool dry_run = false;
    bool force = false;
    ResolverConfig resolver_config;
};

class PackageManager {
public:
    explicit PackageManager(const PackageManagerConfig& config = PackageManagerConfig{});

    // Initialize package manager (create cache dirs, etc.)
    bool initialize();

    // Install all dependencies from manifest
    bool install(const std::filesystem::path& manifest_path);

    // Install a specific package
    bool install_package(const std::string& name, const std::string& version_constraint = "",
                         DependencyKind kind = DependencyKind::Normal);

    // Update all dependencies
    bool update(const std::filesystem::path& manifest_path);

    // Update a specific package
    bool update_package(const std::string& name);

    // Remove a dependency from manifest and uninstall
    bool remove(const std::filesystem::path& manifest_path, const std::string& name);

    // Add a dependency to manifest
    bool add_dependency(const std::filesystem::path& manifest_path,
                        const std::string& name,
                        const std::string& version_constraint,
                        DependencyKind kind = DependencyKind::Normal);

    // Search for packages in registry
    std::vector<std::string> search(const std::string& query, size_t limit = 20);

    // Build package (compile for local use)
    bool build(const std::filesystem::path& manifest_path);

    // Verify lockfile is consistent with manifest
    bool verify(const std::filesystem::path& manifest_path);

    // Clean cache
    bool clean();

    // List installed packages
    std::vector<ResolvedPackage> list_installed();

    // Security audit
    struct SecurityIssue {
        std::string package_name;
        SemVer version;
        std::string severity;  // "critical", "high", "medium", "low"
        std::string description;
        std::string advisory_url;
    };
    std::vector<SecurityIssue> audit();

    // Get install records
    const std::vector<InstallRecord>& get_install_records() const { return install_records_; }

    // Set progress callback
    using ProgressCallback = std::function<void(const std::string& package_name,
                                                 InstallStatus status,
                                                 double progress)>;
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }

    // Get last error
    const std::string& get_last_error() const { return last_error_; }

    // Access components
    PackageCache& get_cache() { return cache_; }
    IPackageRegistry* get_registry() { return registry_.get(); }

private:
    PackageManagerConfig config_;
    PackageCache cache_;
    std::shared_ptr<IPackageRegistry> registry_;
    std::vector<InstallRecord> install_records_;
    ProgressCallback progress_callback_;
    std::string last_error_;

    // Internal helpers
    bool resolve_and_install(const Manifest& manifest, bool update_lockfile = true);
    bool download_and_install(const ResolvedPackage& pkg);
    bool build_package(const ResolvedPackage& pkg);
    bool update_manifest_dependencies(const std::filesystem::path& manifest_path,
                                      const Dependency& dep,
                                      bool remove = false);
    void report_progress(const std::string& package_name, InstallStatus status, double progress);
    void record_install(const std::string& name, const SemVer& version, InstallStatus status,
                        const std::string& error = "");
};

// ============================================================================
// CLI Interface
// ============================================================================

struct PackageCommandResult {
    bool success = false;
    std::string message;
    std::vector<std::string> output_lines;
    int exit_code = 0;
};

class PackageManagerCLI {
public:
    static PackageCommandResult run_command(PackageCommand cmd,
                                             const std::vector<std::string>& args,
                                             const std::filesystem::path& cwd = std::filesystem::current_path());

    static std::string get_command_help(PackageCommand cmd);
    static std::string get_general_help();
};

// ============================================================================
// Utility Functions
// ============================================================================

std::string install_status_to_string(InstallStatus status);

} // namespace package
} // namespace claw

#endif // CLAW_PACKAGE_MANAGER_H
