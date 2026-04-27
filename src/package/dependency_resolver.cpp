// Claw Compiler - Dependency Resolver Implementation
// Constraint satisfaction with backtracking for dependency resolution

#include "package/dependency_resolver.h"
#include "package/lock_file.h"
#include <algorithm>
#include <queue>
#include <sstream>

namespace claw {
namespace package {

// ============================================================================
// ResolvedGraph Implementation
// ============================================================================

ResolvedPackage* ResolvedGraph::find_package(const std::string& name) {
    auto it = name_to_index.find(name);
    if (it != name_to_index.end() && it->second < packages.size()) {
        return &packages[it->second];
    }
    return nullptr;
}

const ResolvedPackage* ResolvedGraph::find_package(const std::string& name) const {
    auto it = name_to_index.find(name);
    if (it != name_to_index.end() && it->second < packages.size()) {
        return &packages[it->second];
    }
    return nullptr;
}

bool ResolvedGraph::has_package(const std::string& name) const {
    return name_to_index.find(name) != name_to_index.end();
}

void ResolvedGraph::add_package(const ResolvedPackage& pkg) {
    if (!has_package(pkg.name)) {
        name_to_index[pkg.name] = packages.size();
        packages.push_back(pkg);
    }
}

std::vector<std::string> ResolvedGraph::get_dependency_order() const {
    // Topological sort using Kahn's algorithm
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& pkg : packages) {
        in_degree[pkg.name] = 0;
    }

    for (const auto& pkg : packages) {
        for (const auto& dep : pkg.dependencies) {
            if (has_package(dep)) {
                adj[dep].push_back(pkg.name);
                in_degree[pkg.name]++;
            }
        }
    }

    std::queue<std::string> q;
    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) q.push(name);
    }

    std::vector<std::string> result;
    while (!q.empty()) {
        auto current = q.front();
        q.pop();
        result.push_back(current);

        auto it = adj.find(current);
        if (it != adj.end()) {
            for (const auto& neighbor : it->second) {
                if (--in_degree[neighbor] == 0) {
                    q.push(neighbor);
                }
            }
        }
    }

    return result;
}

// ============================================================================
// VersionConflict Implementation
// ============================================================================

std::string VersionConflict::to_string() const {
    std::stringstream ss;
    ss << "Version conflict for package '" << package_name << "':\n";
    ss << "  Conflicting constraints:\n";
    for (const auto& c : conflicting_constraints) {
        ss << "    - " << c << "\n";
    }
    ss << "  Dependency paths:\n";
    for (const auto& p : dependency_paths) {
        ss << "    " << p << "\n";
    }
    return ss.str();
}

// ============================================================================
// ResolutionResult Implementation
// ============================================================================

std::string ResolutionResult::get_error_summary() const {
    if (is_success()) return "Resolution successful";

    std::stringstream ss;
    ss << "Resolution failed with " << errors.size() << " error(s)\n";
    for (const auto& e : errors) {
        ss << "  - " << e << "\n";
    }
    for (const auto& c : conflicts) {
        ss << c.to_string() << "\n";
    }
    return ss.str();
}

// ============================================================================
// DependencyResolver Implementation
// ============================================================================

DependencyResolver::DependencyResolver(std::shared_ptr<IPackageRegistry> registry)
    : registry_(std::move(registry)) {}

ResolutionResult DependencyResolver::resolve(
    const Manifest& root_manifest,
    const std::vector<std::string>& enabled_features) {

    ResolverConfig config;
    return resolve_with_config(root_manifest, config, enabled_features);
}

ResolutionResult DependencyResolver::resolve_with_config(
    const Manifest& root_manifest,
    const ResolverConfig& config,
    const std::vector<std::string>& enabled_features) {

    ResolutionResult result;
    errors_.clear();

    if (!root_manifest.parsed) {
        result.status = ResolutionStatus::InvalidConstraint;
        result.errors.push_back("Root manifest is not valid");
        return result;
    }

    ResolutionState state;

    // Resolve root package features
    auto root_features = resolve_features(root_manifest, enabled_features);

    // Collect all direct dependencies from root manifest
    std::vector<std::pair<std::string, std::string>> to_resolve;

    // Normal dependencies
    for (const auto& dep : root_manifest.dependencies) {
        if (!dep.optional) {
            to_resolve.emplace_back(dep.name, dep.version_constraint);
        }
    }

    // Dev dependencies (only if dev features requested)
    for (const auto& dep : root_manifest.dev_dependencies) {
        to_resolve.emplace_back(dep.name, dep.version_constraint);
    }

    // Build dependencies
    for (const auto& dep : root_manifest.build_dependencies) {
        to_resolve.emplace_back(dep.name, dep.version_constraint);
    }

    // Resolve each dependency with transitive resolution
    for (const auto& [name, constraint] : to_resolve) {
        state.resolution_stack = {root_manifest.package.name};
        state.in_progress.clear();

        if (!resolve_package(name, constraint, state, config, root_features)) {
            result.status = ResolutionStatus::Conflict;
            result.errors.insert(result.errors.end(), errors_.begin(), errors_.end());

            // Collect conflicts
            for (const auto& [pkg_name, constraints] : state.constraints) {
                auto versions = registry_->get_versions(pkg_name);
                if (auto conflict = detect_conflict(pkg_name, constraints, versions)) {
                    result.conflicts.push_back(*conflict);
                }
            }

            return result;
        }
    }

    // Check for circular dependencies
    for (const auto& [name, pkg] : state.resolved) {
        for (const auto& dep_name : pkg.dependencies) {
            if (!state.resolved.count(dep_name)) {
                result.warnings.push_back("Unresolved dependency: " + dep_name + " required by " + name);
            }
        }
    }

    // Build result graph
    for (const auto& [name, pkg] : state.resolved) {
        result.graph.add_package(pkg);
    }

    result.status = result.errors.empty() ? ResolutionStatus::Success : ResolutionStatus::Conflict;
    return result;
}

std::optional<ResolvedPackage> DependencyResolver::resolve_single(
    const Dependency& dependency,
    const ResolverConfig& config) {

    ResolutionState state;
    state.resolution_stack = {"<root>"};

    if (!resolve_package(dependency.name, dependency.version_constraint, state, config,
                         dependency.features)) {
        return std::nullopt;
    }

    auto it = state.resolved.find(dependency.name);
    if (it != state.resolved.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool DependencyResolver::resolve_package(
    const std::string& name,
    const std::string& constraint,
    ResolutionState& state,
    const ResolverConfig& config,
    const std::vector<std::string>& parent_features) {

    // Check for circular dependencies
    if (state.in_progress.count(name)) {
        errors_.push_back("Circular dependency detected: " + format_resolution_path(state.resolution_stack) +
                         " -> " + name);
        return false;
    }

    // Check depth limit
    if (state.resolution_stack.size() > config.max_depth) {
        errors_.push_back("Maximum dependency depth exceeded at: " + name);
        return false;
    }

    // Track constraint
    state.constraints[name].push_back(constraint);

    // Check if already resolved
    auto existing = state.resolved.find(name);
    if (existing != state.resolved.end()) {
        // Verify existing version satisfies new constraint
        if (existing->second.version.satisfies(constraint)) {
            return true;
        }

        // Conflict: existing version doesn't satisfy new constraint
        // Try to find a version that satisfies ALL constraints
        auto all_constraints = state.constraints[name];
        auto available = registry_->get_versions(name);

        // Filter versions that satisfy all constraints
        std::vector<SemVer> compatible;
        for (const auto& ver : available) {
            if (version_satisfies_all(ver, all_constraints)) {
                compatible.push_back(ver);
            }
        }

        if (compatible.empty()) {
            // Backtrack
            if (state.backtrack_count < config.backtrack_limit) {
                state.backtrack_count++;
                // Remove existing and try to find better version
                state.resolved.erase(existing);
            } else {
                errors_.push_back("Cannot satisfy constraints for " + name + ": " +
                                 format_resolution_path(state.resolution_stack));
                return false;
            }
        } else {
            // Pick best compatible version
            auto best = select_best_version(name, all_constraints, compatible, config);
            existing->second.version = best;
            return true;
        }
    }

    // Check lockfile for pinned version
    if (config.use_lockfile && lockfile_ && lockfile_->has_package(name)) {
        auto locked = lockfile_->get_package(name);
        if (locked && locked->version.satisfies(constraint)) {
            // Use locked version but still need to resolve its transitive deps
            ResolvedPackage pkg;
            pkg.name = name;
            pkg.version = locked->version;
            pkg.source = locked->source;
            pkg.path = locked->path;

            state.in_progress.insert(name);
            state.resolution_stack.push_back(name);

            // Get manifest to resolve transitive dependencies
            auto manifest = registry_->get_manifest(name, locked->version);
            if (manifest.parsed) {
                auto features = resolve_features(manifest, parent_features);
                for (const auto& dep : manifest.dependencies) {
                    if (!dep.optional || std::find(features.begin(), features.end(), dep.name) != features.end()) {
                        pkg.dependencies.push_back(dep.name);
                        if (!resolve_package(dep.name, dep.version_constraint, state, config, features)) {
                            state.in_progress.erase(name);
                            state.resolution_stack.pop_back();
                            return false;
                        }
                    }
                }
            }

            state.resolved[name] = pkg;
            state.in_progress.erase(name);
            state.resolution_stack.pop_back();
            return true;
        }
    }

    // Get available versions from registry
    auto available = registry_->get_versions(name);
    if (available.empty()) {
        errors_.push_back("Package not found in registry: " + name);
        return false;
    }

    // Filter compatible versions
    std::vector<SemVer> compatible;
    for (const auto& ver : available) {
        if (ver.satisfies(constraint)) {
            compatible.push_back(ver);
        }
    }

    if (compatible.empty()) {
        errors_.push_back("No compatible version found for " + name + " " + constraint);
        return false;
    }

    // Select best version
    auto best_version = select_best_version(name, {constraint}, compatible, config);

    // Create resolved package
    ResolvedPackage pkg;
    pkg.name = name;
    pkg.version = best_version;
    pkg.path = registry_->get_package_path(name, best_version);

    // Determine source type from constraint
    if (constraint.find("path:") == 0) {
        pkg.source = "path";
        pkg.path = constraint.substr(5);
    } else if (constraint.find("git:") == 0) {
        pkg.source = "git";
    } else {
        pkg.source = "registry";
    }

    // Mark as in-progress and add to resolution stack
    state.in_progress.insert(name);
    state.resolution_stack.push_back(name);

    // Resolve transitive dependencies
    auto manifest = registry_->get_manifest(name, best_version);
    if (manifest.parsed) {
        auto features = resolve_features(manifest, parent_features);
        for (const auto& dep : manifest.dependencies) {
            if (!dep.optional || std::find(features.begin(), features.end(), dep.name) != features.end()) {
                pkg.dependencies.push_back(dep.name);
                if (!resolve_package(dep.name, dep.version_constraint, state, config, features)) {
                    state.in_progress.erase(name);
                    state.resolution_stack.pop_back();
                    return false;
                }
            }
        }
    }

    // Success - add to resolved
    state.resolved[name] = pkg;
    state.in_progress.erase(name);
    state.resolution_stack.pop_back();

    return true;
}

SemVer DependencyResolver::select_best_version(
    const std::string& name,
    const std::vector<std::string>& constraints,
    const std::vector<SemVer>& available,
    const ResolverConfig& config) {

    if (available.empty()) return SemVer(0, 0, 0);

    std::vector<SemVer> candidates = available;

    switch (config.strategy) {
        case ResolutionStrategy::Newest:
            std::sort(candidates.begin(), candidates.end(), std::greater<SemVer>());
            break;
        case ResolutionStrategy::Oldest:
            std::sort(candidates.begin(), candidates.end());
            break;
        case ResolutionStrategy::Minimal:
            // Prefer versions that minimize total dependencies (simplified: prefer stable)
            std::sort(candidates.begin(), candidates.end(), [](const SemVer& a, const SemVer& b) {
                if (a.prerelease.empty() != b.prerelease.empty())
                    return a.prerelease.empty(); // stable first
                return a < b;
            });
            break;
        case ResolutionStrategy::Lockfile:
            // Prefer lockfile version if available
            if (lockfile_ && lockfile_->has_package(name)) {
                auto locked = lockfile_->get_package(name);
                if (locked) {
                    for (const auto& v : candidates) {
                        if (v == locked->version) return v;
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end(), std::greater<SemVer>());
            break;
    }

    return candidates.front();
}

bool DependencyResolver::backtrack(ResolutionState& state, const ResolverConfig& config) {
    if (state.resolution_stack.empty() || state.backtrack_count >= config.backtrack_limit) {
        return false;
    }

    state.backtrack_count++;

    // Remove last resolved package to try alternative
    auto last = state.resolution_stack.back();
    state.resolved.erase(last);
    state.in_progress.erase(last);

    return true;
}

std::optional<VersionConflict> DependencyResolver::detect_conflict(
    const std::string& name,
    const std::vector<std::string>& constraints,
    const std::vector<SemVer>& available) {

    bool has_compatible = false;
    for (const auto& ver : available) {
        if (version_satisfies_all(ver, constraints)) {
            has_compatible = true;
            break;
        }
    }

    if (has_compatible) return std::nullopt;

    VersionConflict conflict;
    conflict.package_name = name;
    conflict.conflicting_constraints = constraints;
    return conflict;
}

std::vector<std::string> DependencyResolver::resolve_features(
    const Manifest& manifest,
    const std::vector<std::string>& requested_features) {

    std::vector<std::string> result = manifest.get_enabled_features(requested_features);
    std::unordered_set<std::string> resolved;
    std::queue<std::string> to_process;

    for (const auto& f : result) {
        resolved.insert(f);
        to_process.push(f);
    }

    // Resolve feature dependencies recursively
    while (!to_process.empty()) {
        auto current = to_process.front();
        to_process.pop();

        auto it = manifest.features.find(current);
        if (it != manifest.features.end()) {
            for (const auto& dep : it->second) {
                if (resolved.insert(dep).second) {
                    to_process.push(dep);
                    result.push_back(dep);
                }
            }
        }
    }

    return result;
}

bool DependencyResolver::version_satisfies_all(
    const SemVer& version,
    const std::vector<std::string>& constraints) {

    for (const auto& c : constraints) {
        if (!version.satisfies(c)) return false;
    }
    return true;
}

std::string DependencyResolver::format_resolution_path(const std::vector<std::string>& path) {
    std::string result;
    for (size_t i = 0; i < path.size(); i++) {
        if (i > 0) result += " -> ";
        result += path[i];
    }
    return result;
}

// ============================================================================
// LocalPackageRegistry Implementation
// ============================================================================

LocalPackageRegistry::LocalPackageRegistry(const std::filesystem::path& packages_dir)
    : packages_dir_(packages_dir) {}

std::vector<SemVer> LocalPackageRegistry::get_versions(const std::string& package_name) {
    std::vector<SemVer> result;
    auto it = packages_.find(package_name);
    if (it != packages_.end()) {
        for (const auto& [ver_str, _] : it->second) {
            result.emplace_back(ver_str);
        }
    }
    std::sort(result.begin(), result.end(), std::greater<SemVer>());
    return result;
}

Manifest LocalPackageRegistry::get_manifest(const std::string& package_name, const SemVer& version) {
    auto it = packages_.find(package_name);
    if (it != packages_.end()) {
        auto vit = it->second.find(version.to_string());
        if (vit != it->second.end()) {
            ManifestParser parser;
            return parser.parse_file(vit->second);
        }
    }
    return Manifest();
}

bool LocalPackageRegistry::download_package(const std::string& package_name,
                                            const SemVer& version,
                                            const std::filesystem::path& target_path) {
    auto manifest = get_manifest(package_name, version);
    if (!manifest.parsed) return false;

    // Copy package files to target path
    auto source_dir = manifest.manifest_path.parent_path();
    if (!std::filesystem::exists(target_path)) {
        std::filesystem::create_directories(target_path);
    }

    // Simple copy of all .claw, .h, .cpp files
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".claw" || ext == ".h" || ext == ".cpp" || ext == ".md") {
                auto relative = std::filesystem::relative(entry.path(), source_dir);
                auto dest = target_path / relative;
                std::filesystem::create_directories(dest.parent_path());
                std::filesystem::copy_file(entry.path(), dest,
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
    return true;
}

bool LocalPackageRegistry::has_package(const std::string& package_name) {
    return packages_.find(package_name) != packages_.end();
}

std::filesystem::path LocalPackageRegistry::get_package_path(const std::string& package_name,
                                                              const SemVer& version) {
    auto it = packages_.find(package_name);
    if (it != packages_.end()) {
        auto vit = it->second.find(version.to_string());
        if (vit != it->second.end()) {
            return vit->second.parent_path();
        }
    }
    return packages_dir_ / package_name / version.to_string();
}

void LocalPackageRegistry::register_package(const std::string& name,
                                            const SemVer& version,
                                            const std::filesystem::path& manifest_path) {
    packages_[name][version.to_string()] = manifest_path;
}

// ============================================================================
// PathRegistry Implementation
// ============================================================================

PathRegistry::PathRegistry(const std::filesystem::path& root_path)
    : root_path_(root_path) {}

std::vector<SemVer> PathRegistry::get_versions(const std::string& package_name) {
    auto it = path_mappings_.find(package_name);
    if (it != path_mappings_.end()) {
        // Path-based packages have a single version (0.0.0 or from manifest)
        ManifestParser parser;
        auto manifest = parser.parse_file(it->second / "Claw.toml");
        if (manifest.parsed) {
            return {manifest.package.version};
        }
    }
    return {SemVer(0, 0, 0)};
}

Manifest PathRegistry::get_manifest(const std::string& package_name, const SemVer& version) {
    auto it = path_mappings_.find(package_name);
    if (it != path_mappings_.end()) {
        ManifestParser parser;
        return parser.parse_file(it->second / "Claw.toml");
    }
    return Manifest();
}

bool PathRegistry::download_package(const std::string& package_name,
                                    const SemVer& version,
                                    const std::filesystem::path& target_path) {
    // Path-based packages don't need downloading
    return true;
}

bool PathRegistry::has_package(const std::string& package_name) {
    return path_mappings_.find(package_name) != path_mappings_.end();
}

std::filesystem::path PathRegistry::get_package_path(const std::string& package_name,
                                                      const SemVer& version) {
    auto it = path_mappings_.find(package_name);
    if (it != path_mappings_.end()) {
        return it->second;
    }
    return root_path_ / package_name;
}

void PathRegistry::add_path_mapping(const std::string& name,
                                    const std::filesystem::path& path) {
    path_mappings_[name] = path;
}

} // namespace package
} // namespace claw
