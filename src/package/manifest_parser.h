// Claw Compiler - Package Management System
// Manifest parser for Claw.toml and claw.json configuration files

#ifndef CLAW_PACKAGE_MANIFEST_H
#define CLAW_PACKAGE_MANIFEST_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <filesystem>
#include "common/common.h"

namespace claw {
namespace package {

// ============================================================================
// SemVer Version
// ============================================================================

struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::string prerelease;
    std::string build;

    SemVer() = default;
    SemVer(uint32_t ma, uint32_t mi, uint32_t pa) : major(ma), minor(mi), patch(pa) {}
    SemVer(uint32_t ma, uint32_t mi, uint32_t pa, std::string pre)
        : major(ma), minor(mi), patch(pa), prerelease(std::move(pre)) {}
    explicit SemVer(const std::string& version_str);

    std::string to_string() const;
    bool is_valid() const { return major > 0 || minor > 0 || patch > 0 || !prerelease.empty(); }

    // Comparison operators
    bool operator==(const SemVer& other) const;
    bool operator!=(const SemVer& other) const { return !(*this == other); }
    bool operator<(const SemVer& other) const;
    bool operator<=(const SemVer& other) const { return *this < other || *this == other; }
    bool operator>(const SemVer& other) const { return !(*this <= other); }
    bool operator>=(const SemVer& other) const { return !(*this < other); }

    // Version constraint matching
    bool satisfies(const std::string& constraint) const;
    bool satisfies_caret(const SemVer& base) const;     // ^1.2.3
    bool satisfies_tilde(const SemVer& base) const;     // ~1.2.3
    bool satisfies_range(const SemVer& low, bool low_inc,
                         const SemVer& high, bool high_inc) const;
};

// ============================================================================
// Dependency Specification
// ============================================================================

enum class DependencyKind {
    Normal,         // Regular dependency
    Dev,            // Development-only dependency
    Build,          // Build dependency
    Optional,       // Optional dependency
};

struct Dependency {
    std::string name;                    // Package name
    std::string version_constraint;      // e.g., "^1.2.3", ">=1.0.0 <2.0.0"
    DependencyKind kind = DependencyKind::Normal;
    std::string registry;                // Registry URL or "default"
    std::vector<std::string> features;   // Feature flags to enable
    std::string default_features;        // Feature to enable by default
    bool optional = false;

    Dependency() = default;
    explicit Dependency(const std::string& n) : name(n) {}
    Dependency(const std::string& n, const std::string& vc)
        : name(n), version_constraint(vc) {}
};

// ============================================================================
// Package Target
// ============================================================================

enum class TargetKind {
    Binary,     // Executable
    Library,    // Library crate
    Test,       // Test target
    Benchmark,  // Benchmark target
    Example,    // Example target
};

struct Target {
    std::string name;
    TargetKind kind = TargetKind::Binary;
    std::filesystem::path path;
    std::vector<std::string> required_features;
};

// ============================================================================
// Workspace Configuration
// ============================================================================

struct WorkspaceMember {
    std::string name;
    std::filesystem::path path;
};

struct Workspace {
    std::vector<WorkspaceMember> members;
    std::optional<SemVer> resolver_version;
    std::unordered_map<std::string, Dependency> dependencies; // Shared workspace deps
};

// ============================================================================
// Manifest (Claw.toml)
// ============================================================================

struct PackageMetadata {
    std::string name;
    SemVer version;
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string repository;
    std::string homepage;
    std::string documentation;
    std::vector<std::string> keywords;
    std::vector<std::string> categories;
    std::string readme;
    std::string edition = "2026";  // Language edition
    bool publish = true;
};

struct Manifest {
    PackageMetadata package;
    std::vector<Dependency> dependencies;
    std::vector<Dependency> dev_dependencies;
    std::vector<Dependency> build_dependencies;
    std::vector<Target> targets;
    std::optional<Workspace> workspace;
    std::unordered_map<std::string, std::vector<std::string>> features; // feature -> [deps]
    std::vector<std::string> default_features;
    std::vector<std::string> exclude;  // Files to exclude from package
    std::vector<std::string> include;  // Files to include in package
    std::filesystem::path manifest_path;  // Path to Claw.toml

    // Parsed state
    bool parsed = false;
    std::vector<std::string> parse_errors;

    Manifest() = default;

    // Get all dependencies of a specific kind
    std::vector<Dependency> get_dependencies(DependencyKind kind = DependencyKind::Normal) const;

    // Check if a feature exists
    bool has_feature(const std::string& name) const;

    // Get resolved features (default + enabled)
    std::vector<std::string> get_enabled_features(const std::vector<std::string>& extra = {}) const;
};

// ============================================================================
// Manifest Parser
// ============================================================================

class ManifestParser {
public:
    ManifestParser();

    // Parse from file path
    Manifest parse_file(const std::filesystem::path& path);

    // Parse from string content
    Manifest parse_string(const std::string& content, const std::string& source_name = "<string>");

    // Check if a file is a valid manifest
    bool is_valid_manifest(const std::filesystem::path& path) const;

    // Get last parse errors
    const std::vector<std::string>& get_errors() const { return errors_; }

    // Write manifest to file
    bool write_manifest(const std::filesystem::path& path, const Manifest& manifest);

private:
    std::vector<std::string> errors_;

    // TOML-like parsing (simplified inline implementation)
    struct TomlValue;
    using TomlTable = std::unordered_map<std::string, std::shared_ptr<TomlValue>>;
    using TomlArray = std::vector<std::shared_ptr<TomlValue>>;
    using TomlScalar = std::variant<std::monostate, std::string, int64_t, double, bool>;

    struct TomlValue {
        enum class Type { Table, Array, Scalar } type;
        TomlTable table;
        TomlArray array;
        TomlScalar scalar;

        TomlValue() : type(Type::Scalar) {}
        explicit TomlValue(TomlTable t) : type(Type::Table), table(std::move(t)) {}
        explicit TomlValue(TomlArray a) : type(Type::Array), array(std::move(a)) {}
        explicit TomlValue(TomlScalar s) : type(Type::Scalar), scalar(std::move(s)) {}
    };

    // Parsing helpers
    std::shared_ptr<TomlValue> parse_toml(const std::string& content, const std::string& source_name);
    std::shared_ptr<TomlValue> parse_toml_value(std::istringstream& stream);
    std::shared_ptr<TomlValue> parse_toml_table(std::istringstream& stream);
    std::shared_ptr<TomlValue> parse_toml_array(std::istringstream& stream);
    std::shared_ptr<TomlValue> parse_toml_scalar(const std::string& raw);

    // Manifest construction from parsed TOML
    Manifest build_manifest(const TomlTable& root, const std::filesystem::path& path);
    PackageMetadata parse_package_metadata(const TomlTable& pkg_table);
    std::vector<Dependency> parse_dependencies(const TomlTable& deps_table, DependencyKind kind);
    Dependency parse_dependency_entry(const std::string& name, const std::shared_ptr<TomlValue>& value);
    std::vector<Target> parse_targets(const TomlTable& targets_table);
    std::unordered_map<std::string, std::vector<std::string>> parse_features(const TomlTable& features_table);

    // Utility
    std::string trim(const std::string& s) const;
    std::vector<std::string> split_lines(const std::string& s) const;
    void report_error(const std::string& msg);
};

// ============================================================================
// Manifest Discovery
// ============================================================================

class ManifestDiscovery {
public:
    // Find manifest starting from a path ( walks up directory tree )
    static std::optional<std::filesystem::path> find_manifest(
        const std::filesystem::path& start_path = std::filesystem::current_path());

    // Find all workspace member manifests
    static std::vector<std::filesystem::path> find_workspace_manifests(
        const Manifest& root_manifest);

    // Get default manifest path for a module ID
    static std::filesystem::path module_id_to_manifest_path(
        const std::string& module_id,
        const std::filesystem::path& search_root);
};

// ============================================================================
// Utility Functions
// ============================================================================

std::string dependency_kind_to_string(DependencyKind kind);
DependencyKind string_to_dependency_kind(const std::string& s);
std::string target_kind_to_string(TargetKind kind);

} // namespace package
} // namespace claw

#endif // CLAW_PACKAGE_MANIFEST_H
