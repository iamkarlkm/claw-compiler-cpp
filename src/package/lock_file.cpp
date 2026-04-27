// Claw Compiler - Lock File Implementation
// Ensures reproducible builds with exact version pinning

#include "package/lock_file.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace claw {
namespace package {

// ============================================================================
// LockedPackage Implementation
// ============================================================================

std::string LockedPackage::to_string() const {
    std::stringstream ss;
    ss << "[[package]]\n";
    ss << "name = \"" << name << "\"\n";
    ss << "version = \"" << version.to_string() << "\"\n";
    ss << "source = \"" << source << "\"\n";
    if (!checksum.empty()) ss << "checksum = \"" << checksum << "\"\n";
    if (!registry_url.empty()) ss << "registry-url = \"" << registry_url << "\"\n";
    if (!path.empty()) ss << "path = \"" << path.string() << "\"\n";

    if (!dependencies.empty()) {
        ss << "dependencies = [\n";
        for (const auto& d : dependencies) {
            ss << "  \"" << d << "\",\n";
        }
        ss << "]\n";
    }

    if (!features.empty()) {
        ss << "features = [\n";
        for (const auto& f : features) {
            ss << "  \"" << f << "\",\n";
        }
        ss << "]\n";
    }

    return ss.str();
}

std::optional<LockedPackage> LockedPackage::from_string(const std::string& str) {
    LockedPackage pkg;
    std::istringstream stream(str);
    std::string line;

    while (std::getline(stream, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim
        auto trim = [](const std::string& s) {
            size_t start = s.find_first_not_of(" \"\t");
            size_t end = s.find_last_not_of(" \"\t\n\r");
            if (start == std::string::npos) return std::string();
            return s.substr(start, end - start + 1);
        };

        key = trim(key);
        value = trim(value);

        if (key == "name") pkg.name = value;
        else if (key == "version") pkg.version = SemVer(value);
        else if (key == "source") pkg.source = value;
        else if (key == "checksum") pkg.checksum = value;
        else if (key == "registry-url") pkg.registry_url = value;
        else if (key == "path") pkg.path = value;
    }

    if (pkg.name.empty()) return std::nullopt;
    return pkg;
}

// ============================================================================
// LockFile Implementation
// ============================================================================

LockFile::LockFile() = default;

LockFile::LockFile(const std::filesystem::path& path) {
    load(path);
}

bool LockFile::load(const std::filesystem::path& path) {
    source_path_ = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        last_error_ = "Cannot open lockfile: " + path.string();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return load_from_string(buffer.str());
}

bool LockFile::load_from_string(const std::string& content) {
    return deserialize(content);
}

bool LockFile::save(const std::filesystem::path& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << serialize();
    return true;
}

bool LockFile::has_package(const std::string& name) const {
    return package_index_.find(name) != package_index_.end();
}

std::optional<LockedPackage> LockFile::get_package(const std::string& name) const {
    auto it = package_index_.find(name);
    if (it != package_index_.end() && it->second < packages_.size()) {
        return packages_[it->second];
    }
    return std::nullopt;
}

void LockFile::add_package(const LockedPackage& pkg) {
    auto it = package_index_.find(pkg.name);
    if (it != package_index_.end()) {
        packages_[it->second] = pkg;
    } else {
        package_index_[pkg.name] = packages_.size();
        packages_.push_back(pkg);
    }
}

void LockFile::remove_package(const std::string& name) {
    auto it = package_index_.find(name);
    if (it != package_index_.end()) {
        size_t idx = it->second;
        packages_.erase(packages_.begin() + idx);
        package_index_.erase(it);
        rebuild_index();
    }
}

void LockFile::update_package(const LockedPackage& pkg) {
    add_package(pkg);
}

std::vector<LockedPackage> LockFile::get_ordered_packages() const {
    // Topological sort
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& pkg : packages_) {
        in_degree[pkg.name] = 0;
    }

    for (const auto& pkg : packages_) {
        for (const auto& dep : pkg.dependencies) {
            if (has_package(dep)) {
                adj[dep].push_back(pkg.name);
                in_degree[pkg.name]++;
            }
        }
    }

    std::vector<LockedPackage> result;
    std::vector<std::string> queue;

    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) queue.push_back(name);
    }

    while (!queue.empty()) {
        std::vector<std::string> next_queue;
        for (const auto& name : queue) {
            auto pkg = get_package(name);
            if (pkg) result.push_back(*pkg);

            auto it = adj.find(name);
            if (it != adj.end()) {
                for (const auto& neighbor : it->second) {
                    if (--in_degree[neighbor] == 0) {
                        next_queue.push_back(neighbor);
                    }
                }
            }
        }
        queue = next_queue;
    }

    return result;
}

bool LockFile::is_valid_for_manifest(const Manifest& manifest) const {
    // Check that all manifest dependencies are in lockfile
    for (const auto& dep : manifest.dependencies) {
        if (!dep.optional && !has_package(dep.name)) {
            return false;
        }
    }

    // Check versions still satisfy constraints
    for (const auto& dep : manifest.dependencies) {
        if (dep.optional) continue;
        auto locked = get_package(dep.name);
        if (locked && !locked->version.satisfies(dep.version_constraint)) {
            return false;
        }
    }

    return true;
}

LockFile LockFile::from_resolved_graph(const ResolvedGraph& graph) {
    LockFile lockfile;
    for (const auto& pkg : graph.packages) {
        LockedPackage locked;
        locked.name = pkg.name;
        locked.version = pkg.version;
        locked.source = pkg.source;
        locked.path = pkg.path;
        locked.dependencies = pkg.dependencies;
        lockfile.add_package(locked);
    }
    return lockfile;
}

std::string LockFile::get_metadata(const std::string& key) const {
    auto it = metadata_.find(key);
    if (it != metadata_.end()) return it->second;
    return "";
}

void LockFile::set_metadata(const std::string& key, const std::string& value) {
    metadata_[key] = value;
}

bool LockFile::is_newer_than(const std::filesystem::path& manifest_path) const {
    if (!std::filesystem::exists(source_path_) || !std::filesystem::exists(manifest_path)) {
        return false;
    }
    auto lock_time = std::filesystem::last_write_time(source_path_);
    auto manifest_time = std::filesystem::last_write_time(manifest_path);
    return lock_time > manifest_time;
}

void LockFile::clear() {
    packages_.clear();
    package_index_.clear();
    metadata_.clear();
}

std::string LockFile::compute_checksum(const std::filesystem::path& package_path) {
    // Placeholder: In production, compute SHA-256 of all package files
    // For now, return a hash based on path and modification time
    if (!std::filesystem::exists(package_path)) return "";

    auto time = std::filesystem::last_write_time(package_path);
    auto time_val = std::chrono::duration_cast<std::chrono::seconds>(
        time.time_since_epoch()).count();

    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << time_val;
    return ss.str();
}

void LockFile::rebuild_index() {
    package_index_.clear();
    for (size_t i = 0; i < packages_.size(); i++) {
        package_index_[packages_[i].name] = i;
    }
}

std::string LockFile::serialize() const {
    std::stringstream ss;
    ss << "# This file is automatically generated by Claw.\n";
    ss << "# It is not intended for manual editing.\n";
    ss << "version = \"" << version_ << "\"\n";

    for (const auto& [key, value] : metadata_) {
        ss << "# " << key << " = \"" << value << "\"\n";
    }

    ss << "\n";

    for (const auto& pkg : get_ordered_packages()) {
        ss << pkg.to_string() << "\n";
    }

    return ss.str();
}

bool LockFile::deserialize(const std::string& content) {
    packages_.clear();
    package_index_.clear();

    std::istringstream stream(content);
    std::string line;
    std::string current_block;
    bool in_package = false;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;

        if (line == "[[package]]") {
            if (in_package && !current_block.empty()) {
                auto pkg = LockedPackage::from_string(current_block);
                if (pkg) add_package(*pkg);
            }
            in_package = true;
            current_block.clear();
            continue;
        }

        if (in_package) {
            current_block += line + "\n";
        } else {
            // Parse top-level metadata
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                // Trim
                auto trim = [](const std::string& s) {
                    size_t start = s.find_first_not_of(" \"\t");
                    size_t end = s.find_last_not_of(" \"\t\n\r");
                    if (start == std::string::npos) return std::string();
                    return s.substr(start, end - start + 1);
                };
                key = trim(key);
                value = trim(value);
                if (key == "version") version_ = value;
                else metadata_[key] = value;
            }
        }
    }

    if (in_package && !current_block.empty()) {
        auto pkg = LockedPackage::from_string(current_block);
        if (pkg) add_package(*pkg);
    }

    return true;
}

// ============================================================================
// LockFileManager Implementation
// ============================================================================

std::filesystem::path LockFileManager::get_lockfile_path(const std::filesystem::path& manifest_path) {
    auto dir = manifest_path.parent_path();
    auto stem = manifest_path.stem().string();
    if (stem == "Claw" || stem == "claw") {
        return dir / "Claw.lock";
    }
    return dir / (stem + ".lock");
}

bool LockFileManager::lockfile_exists(const std::filesystem::path& manifest_path) {
    return std::filesystem::exists(get_lockfile_path(manifest_path));
}

LockFile LockFileManager::load_or_create(const std::filesystem::path& manifest_path) {
    auto lock_path = get_lockfile_path(manifest_path);
    LockFile lockfile;
    if (std::filesystem::exists(lock_path)) {
        lockfile.load(lock_path);
    }
    return lockfile;
}

bool LockFileManager::update_lockfile(const std::filesystem::path& manifest_path,
                                       const ResolutionResult& result) {
    if (result.status != ResolutionStatus::Success) return false;

    auto lock = LockFile::from_resolved_graph(result.graph);
    auto lock_path = get_lockfile_path(manifest_path);
    return lock.save(lock_path);
}

bool LockFileManager::prune_lockfile(const std::filesystem::path& manifest_path,
                                      const Manifest& manifest) {
    auto lock_path = get_lockfile_path(manifest_path);
    LockFile lockfile(lock_path);

    std::unordered_set<std::string> needed;
    for (const auto& dep : manifest.dependencies) {
        needed.insert(dep.name);
    }
    for (const auto& dep : manifest.dev_dependencies) {
        needed.insert(dep.name);
    }
    for (const auto& dep : manifest.build_dependencies) {
        needed.insert(dep.name);
    }

    bool changed = false;
    for (const auto& pkg : lockfile.get_packages()) {
        if (!needed.count(pkg.name)) {
            lockfile.remove_package(pkg.name);
            changed = true;
        }
    }

    if (changed) {
        return lockfile.save(lock_path);
    }
    return true;
}

} // namespace package
} // namespace claw
