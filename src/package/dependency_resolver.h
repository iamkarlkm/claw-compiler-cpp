// Claw Compiler - Dependency Resolver
// Resolves transitive dependencies with SemVer constraints and conflict detection

#ifndef CLAW_DEPENDENCY_RESOLVER_H
#define CLAW_DEPENDENCY_RESOLVER_H

#include "package/manifest_parser.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>

namespace claw {
namespace package {

// ============================================================================
// Resolved Dependency Graph
// ============================================================================

struct ResolvedPackage {
    std::string name;
    SemVer version;
    std::string source;          // "registry", "path", "git"
    std::filesystem::path path;  // Local path or cache path
    std::string registry_url;
    std::vector<std::string> features;
    std::vector<std::string> dependencies;  // Names of dependent packages

    bool operator==(const ResolvedPackage& other) const {
        return name == other.name && version == other.version;
    }
};

struct ResolvedGraph {
    std::vector<ResolvedPackage> packages;
    std::unordered_map<std::string, size_t> name_to_index;  // name -> index in packages

    ResolvedPackage* find_package(const std::string& name);
    const ResolvedPackage* find_package(const std::string& name) const;
    bool has_package(const std::string& name) const;
    void add_package(const ResolvedPackage& pkg);
    std::vector<std::string> get_dependency_order() const;  // Topological sort
};

// ============================================================================
// Version Conflict
// ============================================================================

struct VersionConflict {
    std::string package_name;
    std::vector<std::string> conflicting_constraints;
    std::vector<std::string> dependency_paths;  // How we reached this conflict

    std::string to_string() const;
};

// ============================================================================
// Resolution Result
// ============================================================================

enum class ResolutionStatus {
    Success,
    Conflict,
    MissingPackage,
    CircularDependency,
    InvalidConstraint,
    RegistryError
};

struct ResolutionResult {
    ResolutionStatus status = ResolutionStatus::Success;
    ResolvedGraph graph;
    std::vector<VersionConflict> conflicts;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool is_success() const { return status == ResolutionStatus::Success && errors.empty(); }
    std::string get_error_summary() const;
};

// ============================================================================
// Package Registry Interface
// ============================================================================

class IPackageRegistry {
public:
    virtual ~IPackageRegistry() = default;

    // Get all available versions of a package
    virtual std::vector<SemVer> get_versions(const std::string& package_name) = 0;

    // Get manifest for a specific version
    virtual Manifest get_manifest(const std::string& package_name, const SemVer& version) = 0;

    // Download package to local cache
    virtual bool download_package(const std::string& package_name, const SemVer& version,
                                   const std::filesystem::path& target_path) = 0;

    // Check if package exists
    virtual bool has_package(const std::string& package_name) = 0;

    // Get package path in cache
    virtual std::filesystem::path get_package_path(const std::string& package_name,
                                                    const SemVer& version) = 0;
};

// ============================================================================
// Dependency Resolver
// ============================================================================

enum class ResolutionStrategy {
    Newest,       // Prefer newest compatible version
    Oldest,       // Prefer oldest compatible version
    Minimal,      // Minimize total dependencies
    Lockfile,     // Prefer versions from lock file
};

struct ResolverConfig {
    ResolutionStrategy strategy = ResolutionStrategy::Newest;
    bool allow_prerelease = false;
    bool allow_yanked = false;
    size_t max_depth = 50;           // Max transitive dependency depth
    size_t backtrack_limit = 1000;   // Max backtracking steps
    bool use_lockfile = true;        // Prefer lockfile versions
    bool strict_semver = true;       // Enforce strict SemVer
};

class DependencyResolver {
public:
    explicit DependencyResolver(std::shared_ptr<IPackageRegistry> registry);

    // Main resolution entry point
    ResolutionResult resolve(const Manifest& root_manifest,
                             const std::vector<std::string>& enabled_features = {});

    // Resolve with custom config
    ResolutionResult resolve_with_config(const Manifest& root_manifest,
                                          const ResolverConfig& config,
                                          const std::vector<std::string>& enabled_features = {});

    // Resolve a single dependency
    std::optional<ResolvedPackage> resolve_single(
        const Dependency& dependency,
        const ResolverConfig& config = ResolverConfig{});

    // Get resolver errors
    const std::vector<std::string>& get_errors() const { return errors_; }

    // Set lockfile for version pinning
    void set_lockfile(const class LockFile* lockfile) { lockfile_ = lockfile; }

private:
    std::shared_ptr<IPackageRegistry> registry_;
    const class LockFile* lockfile_ = nullptr;
    std::vector<std::string> errors_;

    // Resolution state
    struct ResolutionState {
        std::unordered_map<std::string, ResolvedPackage> resolved;  // name -> package
        std::unordered_map<std::string, std::vector<std::string>> constraints;  // name -> [constraints]
        std::unordered_set<std::string> in_progress;  // Packages currently being resolved (cycle detection)
        std::vector<std::string> resolution_stack;    // Current resolution path
        size_t backtrack_count = 0;
        size_t max_depth_reached = 0;
    };

    // Core resolution algorithm
    bool resolve_package(const std::string& name,
                         const std::string& constraint,
                         ResolutionState& state,
                         const ResolverConfig& config,
                         const std::vector<std::string>& parent_features);

    // Version selection
    SemVer select_best_version(const std::string& name,
                                const std::vector<std::string>& constraints,
                                const std::vector<SemVer>& available,
                                const ResolverConfig& config);

    // Backtracking
    bool backtrack(ResolutionState& state, const ResolverConfig& config);

    // Conflict detection
    std::optional<VersionConflict> detect_conflict(
        const std::string& name,
        const std::vector<std::string>& constraints,
        const std::vector<SemVer>& available);

    // Feature resolution
    std::vector<std::string> resolve_features(
        const Manifest& manifest,
        const std::vector<std::string>& requested_features);

    // Utility
    bool version_satisfies_all(const SemVer& version,
                                const std::vector<std::string>& constraints);
    std::string format_resolution_path(const std::vector<std::string>& path);
};

// ============================================================================
// Local Package Registry
// ============================================================================

class LocalPackageRegistry : public IPackageRegistry {
public:
    explicit LocalPackageRegistry(const std::filesystem::path& packages_dir);

    std::vector<SemVer> get_versions(const std::string& package_name) override;
    Manifest get_manifest(const std::string& package_name, const SemVer& version) override;
    bool download_package(const std::string& package_name, const SemVer& version,
                          const std::filesystem::path& target_path) override;
    bool has_package(const std::string& package_name) override;
    std::filesystem::path get_package_path(const std::string& package_name,
                                            const SemVer& version) override;

    // Register a local package
    void register_package(const std::string& name, const SemVer& version,
                          const std::filesystem::path& manifest_path);

private:
    std::filesystem::path packages_dir_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::filesystem::path>> packages_;
    // name -> {version_str -> manifest_path}
};

// ============================================================================
// Path-based Registry
// ============================================================================

class PathRegistry : public IPackageRegistry {
public:
    explicit PathRegistry(const std::filesystem::path& root_path);

    std::vector<SemVer> get_versions(const std::string& package_name) override;
    Manifest get_manifest(const std::string& package_name, const SemVer& version) override;
    bool download_package(const std::string& package_name, const SemVer& version,
                          const std::filesystem::path& target_path) override;
    bool has_package(const std::string& package_name) override;
    std::filesystem::path get_package_path(const std::string& package_name,
                                            const SemVer& version) override;

    // Map package name to path
    void add_path_mapping(const std::string& name, const std::filesystem::path& path);

private:
    std::filesystem::path root_path_;
    std::unordered_map<std::string, std::filesystem::path> path_mappings_;
};

} // namespace package
} // namespace claw

#endif // CLAW_DEPENDENCY_RESOLVER_H
