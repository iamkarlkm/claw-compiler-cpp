// Claw Compiler - Package Manager Implementation
// Central orchestrator for all package operations

#include "package/package_manager.h"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace claw {
namespace package {

// ============================================================================
// PackageCache Implementation
// ============================================================================

PackageCache::PackageCache(const std::filesystem::path& cache_dir)
    : cache_dir_(cache_dir) {}

std::filesystem::path PackageCache::get_package_path(const std::string& name,
                                                      const SemVer& version) const {
    return cache_dir_ / name / version.to_string();
}

bool PackageCache::is_cached(const std::string& name, const SemVer& version) const {
    auto path = get_package_path(name, version);
    return std::filesystem::exists(path / "Claw.toml");
}

bool PackageCache::cache_package(const std::string& name, const SemVer& version,
                                  const std::filesystem::path& source_path) {
    auto target = get_package_path(name, version);
    try {
        std::filesystem::create_directories(target);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source_path)) {
            if (entry.is_regular_file()) {
                auto relative = std::filesystem::relative(entry.path(), source_path);
                auto dest = target / relative;
                std::filesystem::create_directories(dest.parent_path());
                std::filesystem::copy_file(entry.path(), dest,
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool PackageCache::remove_from_cache(const std::string& name, const SemVer& version) {
    auto path = get_package_path(name, version);
    try {
        return std::filesystem::remove_all(path) > 0;
    } catch (...) {
        return false;
    }
}

size_t PackageCache::clean_old_versions(const std::string& name, size_t keep_count) {
    auto pkg_dir = cache_dir_ / name;
    if (!std::filesystem::exists(pkg_dir)) return 0;

    std::vector<std::pair<SemVer, std::filesystem::path>> versions;
    for (const auto& entry : std::filesystem::directory_iterator(pkg_dir)) {
        if (entry.is_directory()) {
            versions.emplace_back(SemVer(entry.path().filename().string()), entry.path());
        }
    }

    if (versions.size() <= keep_count) return 0;

    std::sort(versions.begin(), versions.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    size_t removed = 0;
    for (size_t i = keep_count; i < versions.size(); i++) {
        try {
            removed += std::filesystem::remove_all(versions[i].second);
        } catch (...) {}
    }
    return removed;
}

std::vector<SemVer> PackageCache::get_cached_versions(const std::string& name) const {
    std::vector<SemVer> result;
    auto pkg_dir = cache_dir_ / name;
    if (!std::filesystem::exists(pkg_dir)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(pkg_dir)) {
        if (entry.is_directory()) {
            result.emplace_back(entry.path().filename().string());
        }
    }
    std::sort(result.begin(), result.end(), std::greater<SemVer>());
    return result;
}

size_t PackageCache::get_cache_size() const {
    size_t total = 0;
    if (!std::filesystem::exists(cache_dir_)) return 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(cache_dir_)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    return total;
}

bool PackageCache::clear_cache() {
    try {
        std::filesystem::remove_all(cache_dir_);
        std::filesystem::create_directories(cache_dir_);
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// PackageManager Implementation
// ============================================================================

PackageManager::PackageManager(const PackageManagerConfig& config)
    : config_(config),
      cache_(config.cache_dir.empty() ? std::filesystem::temp_directory_path() / "claw_cache"
                                       : config.cache_dir) {}

bool PackageManager::initialize() {
    try {
        std::filesystem::create_directories(cache_.get_cache_dir());

        // Initialize default registry (local filesystem)
        auto local_registry = std::make_shared<LocalPackageRegistry>(cache_.get_cache_dir());
        registry_ = local_registry;

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Initialization failed: ") + e.what();
        return false;
    }
}

bool PackageManager::install(const std::filesystem::path& manifest_path) {
    install_records_.clear();

    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) {
        last_error_ = "Failed to parse manifest: " + manifest_path.string();
        return false;
    }

    return resolve_and_install(manifest, true);
}

bool PackageManager::install_package(const std::string& name,
                                      const std::string& version_constraint,
                                      DependencyKind kind) {
    Dependency dep(name, version_constraint.empty() ? "*" : version_constraint);
    dep.kind = kind;

    // Resolve single dependency
    DependencyResolver resolver(registry_);
    auto resolved = resolver.resolve_single(dep);
    if (!resolved) {
        last_error_ = "Failed to resolve package: " + name;
        return false;
    }

    report_progress(name, InstallStatus::Downloading, 0.0);
    bool success = download_and_install(*resolved);
    report_progress(name, success ? InstallStatus::Installed : InstallStatus::Failed,
                    success ? 1.0 : 0.0);

    record_install(name, resolved->version,
                   success ? InstallStatus::Installed : InstallStatus::Failed,
                   success ? "" : last_error_);

    return success;
}

bool PackageManager::update(const std::filesystem::path& manifest_path) {
    install_records_.clear();

    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) {
        last_error_ = "Failed to parse manifest: " + manifest_path.string();
        return false;
    }

    // Force update by ignoring lockfile
    auto resolver_config = config_.resolver_config;
    resolver_config.use_lockfile = false;
    DependencyResolver resolver(registry_);
    resolver.set_lockfile(nullptr);

    auto result = resolver.resolve_with_config(manifest, resolver_config);
    if (!result.is_success()) {
        last_error_ = result.get_error_summary();
        return false;
    }

    // Update lockfile
    LockFileManager::update_lockfile(manifest_path, result);

    // Install/update packages
    for (const auto& pkg : result.graph.packages) {
        if (cache_.is_cached(pkg.name, pkg.version)) {
            report_progress(pkg.name, InstallStatus::Cached, 1.0);
            record_install(pkg.name, pkg.version, InstallStatus::Cached);
            continue;
        }

        report_progress(pkg.name, InstallStatus::Downloading, 0.0);
        bool success = download_and_install(pkg);
        report_progress(pkg.name, success ? InstallStatus::Installed : InstallStatus::Failed,
                        success ? 1.0 : 0.0);
        record_install(pkg.name, pkg.version,
                       success ? InstallStatus::Installed : InstallStatus::Failed,
                       success ? "" : last_error_);
    }

    return true;
}

bool PackageManager::update_package(const std::string& name) {
    // Find latest compatible version and install
    auto versions = registry_->get_versions(name);
    if (versions.empty()) {
        last_error_ = "Package not found: " + name;
        return false;
    }

    auto latest = versions.front();
    ResolvedPackage pkg;
    pkg.name = name;
    pkg.version = latest;
    pkg.path = registry_->get_package_path(name, latest);

    return download_and_install(pkg);
}

bool PackageManager::remove(const std::filesystem::path& manifest_path, const std::string& name) {
    // Update manifest
    Dependency dep(name, "");
    if (!update_manifest_dependencies(manifest_path, dep, true)) {
        last_error_ = "Failed to update manifest";
        return false;
    }

    // Remove from cache
    auto cached = cache_.get_cached_versions(name);
    for (const auto& ver : cached) {
        cache_.remove_from_cache(name, ver);
    }

    return true;
}

bool PackageManager::add_dependency(const std::filesystem::path& manifest_path,
                                    const std::string& name,
                                    const std::string& version_constraint,
                                    DependencyKind kind) {
    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) {
        last_error_ = "Failed to parse manifest";
        return false;
    }

    // Check if already exists
    auto& deps = (kind == DependencyKind::Dev) ? manifest.dev_dependencies :
                 (kind == DependencyKind::Build) ? manifest.build_dependencies :
                 manifest.dependencies;

    for (auto& dep : deps) {
        if (dep.name == name) {
            dep.version_constraint = version_constraint;
            return parser.write_manifest(manifest_path, manifest);
        }
    }

    // Add new dependency
    Dependency dep(name, version_constraint);
    dep.kind = kind;
    deps.push_back(dep);

    return parser.write_manifest(manifest_path, manifest);
}

std::vector<std::string> PackageManager::search(const std::string& query, size_t limit) {
    std::vector<std::string> results;
    // This is a simplified search - in production, query a registry API
    // For now, search cached packages
    if (!std::filesystem::exists(cache_.get_cache_dir())) return results;

    for (const auto& entry : std::filesystem::directory_iterator(cache_.get_cache_dir())) {
        if (entry.is_directory()) {
            auto name = entry.path().filename().string();
            if (name.find(query) != std::string::npos) {
                results.push_back(name);
                if (results.size() >= limit) break;
            }
        }
    }
    return results;
}

bool PackageManager::build(const std::filesystem::path& manifest_path) {
    // Simplified build - in production, invoke compiler pipeline
    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) {
        last_error_ = "Failed to parse manifest";
        return false;
    }

    // Ensure dependencies are installed
    if (!install(manifest_path)) {
        return false;
    }

    // TODO: Invoke actual compiler build
    return true;
}

bool PackageManager::verify(const std::filesystem::path& manifest_path) {
    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) {
        last_error_ = "Failed to parse manifest";
        return false;
    }

    auto lock_path = LockFileManager::get_lockfile_path(manifest_path);
    if (!std::filesystem::exists(lock_path)) {
        last_error_ = "Lockfile not found";
        return false;
    }

    LockFile lockfile(lock_path);
    return lockfile.is_valid_for_manifest(manifest);
}

bool PackageManager::clean() {
    return cache_.clear_cache();
}

std::vector<ResolvedPackage> PackageManager::list_installed() {
    std::vector<ResolvedPackage> result;
    auto cache_dir = cache_.get_cache_dir();
    if (!std::filesystem::exists(cache_dir)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (!entry.is_directory()) continue;

        auto name = entry.path().filename().string();
        for (const auto& ver_entry : std::filesystem::directory_iterator(entry.path())) {
            if (!ver_entry.is_directory()) continue;

            auto version = SemVer(ver_entry.path().filename().string());
            ResolvedPackage pkg;
            pkg.name = name;
            pkg.version = version;
            pkg.path = ver_entry.path();
            result.push_back(pkg);
        }
    }

    return result;
}

std::vector<PackageManager::SecurityIssue> PackageManager::audit() {
    std::vector<SecurityIssue> issues;
    // Placeholder: In production, query security advisory database
    // For now, return empty list
    return issues;
}

// ============================================================================
// Internal Helpers
// ============================================================================

bool PackageManager::resolve_and_install(const Manifest& manifest, bool update_lock) {
    // Load lockfile if exists
    auto lock_path = LockFileManager::get_lockfile_path(manifest.manifest_path);
    LockFile lockfile;
    bool has_lockfile = false;

    if (config_.resolver_config.use_lockfile && std::filesystem::exists(lock_path)) {
        lockfile.load(lock_path);
        has_lockfile = true;
    }

    // Resolve dependencies
    DependencyResolver resolver(registry_);
    if (has_lockfile) {
        resolver.set_lockfile(&lockfile);
    }

    auto result = resolver.resolve_with_config(manifest, config_.resolver_config);
    if (!result.is_success()) {
        last_error_ = result.get_error_summary();
        return false;
    }

    // Update lockfile
    if (update_lock) {
        LockFileManager::update_lockfile(manifest.manifest_path, result);
    }

    // Install each package
    for (const auto& pkg : result.graph.packages) {
        if (cache_.is_cached(pkg.name, pkg.version)) {
            report_progress(pkg.name, InstallStatus::Cached, 1.0);
            record_install(pkg.name, pkg.version, InstallStatus::Cached);
            continue;
        }

        report_progress(pkg.name, InstallStatus::Downloading, 0.0);
        bool success = download_and_install(pkg);
        report_progress(pkg.name, success ? InstallStatus::Installed : InstallStatus::Failed,
                        success ? 1.0 : 0.0);
        record_install(pkg.name, pkg.version,
                       success ? InstallStatus::Installed : InstallStatus::Failed,
                       success ? "" : last_error_);
    }

    return true;
}

bool PackageManager::download_and_install(const ResolvedPackage& pkg) {
    if (config_.dry_run) return true;
    if (config_.offline_mode) {
        last_error_ = "Cannot download in offline mode";
        return false;
    }

    try {
        auto target = cache_.get_package_path(pkg.name, pkg.version);
        std::filesystem::create_directories(target);

        if (pkg.source == "path") {
            // Copy from local path
            if (!std::filesystem::exists(pkg.path)) {
                last_error_ = "Local path does not exist: " + pkg.path.string();
                return false;
            }
            cache_.cache_package(pkg.name, pkg.version, pkg.path);
        } else {
            // Download from registry
            if (!registry_->download_package(pkg.name, pkg.version, target)) {
                last_error_ = "Failed to download package: " + pkg.name;
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Installation error: ") + e.what();
        return false;
    }
}

bool PackageManager::build_package(const ResolvedPackage& pkg) {
    // Placeholder: In production, invoke compiler on package source
    (void)pkg;
    return true;
}

bool PackageManager::update_manifest_dependencies(const std::filesystem::path& manifest_path,
                                                   const Dependency& dep,
                                                   bool remove) {
    ManifestParser parser;
    auto manifest = parser.parse_file(manifest_path);
    if (!manifest.parsed) return false;

    bool changed = false;

    auto remove_dep = [&](std::vector<Dependency>& deps) {
        auto it = std::remove_if(deps.begin(), deps.end(),
                                 [&](const Dependency& d) { return d.name == dep.name; });
        if (it != deps.end()) {
            deps.erase(it, deps.end());
            changed = true;
        }
    };

    if (remove) {
        remove_dep(manifest.dependencies);
        remove_dep(manifest.dev_dependencies);
        remove_dep(manifest.build_dependencies);
    } else {
        manifest.dependencies.push_back(dep);
        changed = true;
    }

    return changed ? parser.write_manifest(manifest_path, manifest) : true;
}

void PackageManager::report_progress(const std::string& package_name,
                                      InstallStatus status, double progress) {
    if (progress_callback_) {
        progress_callback_(package_name, status, progress);
    }
}

void PackageManager::record_install(const std::string& name, const SemVer& version,
                                     InstallStatus status, const std::string& error) {
    InstallRecord record;
    record.package_name = name;
    record.version = version;
    record.status = status;
    record.error_message = error;
    record.end_time = std::chrono::steady_clock::now();

    if (!install_records_.empty() && install_records_.back().package_name == name) {
        record.start_time = install_records_.back().start_time;
    } else {
        record.start_time = record.end_time;
    }

    install_records_.push_back(record);
}

// ============================================================================
// PackageManagerCLI Implementation
// ============================================================================

PackageCommandResult PackageManagerCLI::run_command(PackageCommand cmd,
                                                     const std::vector<std::string>& args,
                                                     const std::filesystem::path& cwd) {
    PackageCommandResult result;
    PackageManagerConfig config;
    config.cache_dir = cwd / ".claw" / "cache";

    PackageManager pm(config);
    if (!pm.initialize()) {
        result.success = false;
        result.message = "Failed to initialize package manager: " + pm.get_last_error();
        result.exit_code = 1;
        return result;
    }

    auto manifest_path = ManifestDiscovery::find_manifest(cwd);
    if (!manifest_path && cmd != PackageCommand::Search && cmd != PackageCommand::Clean) {
        result.success = false;
        result.message = "No manifest found in current directory or parent directories";
        result.exit_code = 1;
        return result;
    }

    switch (cmd) {
        case PackageCommand::Install: {
            if (args.empty()) {
                result.success = pm.install(*manifest_path);
            } else {
                std::string version = args.size() > 1 ? args[1] : "";
                result.success = pm.install_package(args[0], version);
            }
            if (!result.success) {
                result.message = pm.get_last_error();
                result.exit_code = 1;
            }
            break;
        }
        case PackageCommand::Update: {
            if (args.empty()) {
                result.success = pm.update(*manifest_path);
            } else {
                result.success = pm.update_package(args[0]);
            }
            if (!result.success) {
                result.message = pm.get_last_error();
                result.exit_code = 1;
            }
            break;
        }
        case PackageCommand::Remove: {
            if (args.empty()) {
                result.message = "Usage: claw remove <package>";
                result.exit_code = 1;
                break;
            }
            result.success = pm.remove(*manifest_path, args[0]);
            if (!result.success) {
                result.message = pm.get_last_error();
                result.exit_code = 1;
            }
            break;
        }
        case PackageCommand::Add: {
            if (args.size() < 2) {
                result.message = "Usage: claw add <package> <version> [dev|build]";
                result.exit_code = 1;
                break;
            }
            auto kind = DependencyKind::Normal;
            if (args.size() > 2) kind = string_to_dependency_kind(args[2]);
            result.success = pm.add_dependency(*manifest_path, args[0], args[1], kind);
            if (!result.success) {
                result.message = pm.get_last_error();
                result.exit_code = 1;
            }
            break;
        }
        case PackageCommand::Search: {
            if (args.empty()) {
                result.message = "Usage: claw search <query>";
                result.exit_code = 1;
                break;
            }
            auto packages = pm.search(args[0]);
            for (const auto& pkg : packages) {
                result.output_lines.push_back(pkg);
            }
            result.success = true;
            break;
        }
        case PackageCommand::Clean: {
            result.success = pm.clean();
            result.message = result.success ? "Cache cleaned" : "Failed to clean cache";
            break;
        }
        case PackageCommand::List: {
            auto packages = pm.list_installed();
            for (const auto& pkg : packages) {
                result.output_lines.push_back(pkg.name + " v" + pkg.version.to_string() +
                                               " @ " + pkg.path.string());
            }
            result.success = true;
            break;
        }
        case PackageCommand::Verify: {
            result.success = pm.verify(*manifest_path);
            result.message = result.success ? "Lockfile is valid" : "Lockfile is outdated";
            break;
        }
        case PackageCommand::Audit: {
            auto issues = pm.audit();
            if (issues.empty()) {
                result.message = "No security issues found";
            } else {
                for (const auto& issue : issues) {
                    result.output_lines.push_back("[" + issue.severity + "] " +
                                                   issue.package_name + " " +
                                                   issue.version.to_string() + ": " +
                                                   issue.description);
                }
            }
            result.success = true;
            break;
        }
        default:
            result.message = "Unknown command";
            result.exit_code = 1;
    }

    return result;
}

std::string PackageManagerCLI::get_command_help(PackageCommand cmd) {
    switch (cmd) {
        case PackageCommand::Install: return "install [package] [version] - Install dependencies";
        case PackageCommand::Update: return "update [package] - Update dependencies";
        case PackageCommand::Remove: return "remove <package> - Remove a dependency";
        case PackageCommand::Add: return "add <package> <version> [kind] - Add dependency";
        case PackageCommand::Search: return "search <query> - Search packages";
        case PackageCommand::Publish: return "publish - Publish package to registry";
        case PackageCommand::Clean: return "clean - Clean package cache";
        case PackageCommand::List: return "list - List installed packages";
        case PackageCommand::Verify: return "verify - Verify lockfile consistency";
        case PackageCommand::Audit: return "audit - Security audit";
        default: return "Unknown command";
    }
}

std::string PackageManagerCLI::get_general_help() {
    return R"(Claw Package Manager

Usage: claw pkg <command> [options]

Commands:
  install [pkg] [ver]  Install dependencies or specific package
  update [pkg]         Update all or specific dependency
  remove <pkg>         Remove a dependency
  add <pkg> <ver>      Add dependency to manifest
  search <query>       Search for packages
  clean                Clean package cache
  list                 List installed packages
  verify               Verify lockfile consistency
  audit                Security audit

Options:
  --offline            Don't download packages
  --verbose            Verbose output
  --dry-run            Simulate without making changes
)";
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string install_status_to_string(InstallStatus status) {
    switch (status) {
        case InstallStatus::Pending: return "pending";
        case InstallStatus::Downloading: return "downloading";
        case InstallStatus::Extracting: return "extracting";
        case InstallStatus::Building: return "building";
        case InstallStatus::Installed: return "installed";
        case InstallStatus::Failed: return "failed";
        case InstallStatus::Cached: return "cached";
        case InstallStatus::Skipped: return "skipped";
        default: return "unknown";
    }
}

} // namespace package
} // namespace claw
