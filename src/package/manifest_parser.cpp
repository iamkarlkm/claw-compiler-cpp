// Claw Compiler - Manifest Parser Implementation
// Supports Claw.toml format (TOML-like simplified syntax)

#include "package/manifest_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace claw {
namespace package {

// ============================================================================
// SemVer Implementation
// ============================================================================

SemVer::SemVer(const std::string& version_str) {
    // Parse version string: major.minor.patch[-prerelease][+build]
    std::regex ver_regex(R"((\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-([a-zA-Z0-9.]+))?(?:\+([a-zA-Z0-9.]+))?)");
    std::smatch match;
    if (std::regex_match(version_str, match, ver_regex)) {
        major = std::stoul(match[1].str());
        if (match[2].matched) minor = std::stoul(match[2].str());
        if (match[3].matched) patch = std::stoul(match[3].str());
        if (match[4].matched) prerelease = match[4].str();
        if (match[5].matched) build = match[5].str();
    }
}

std::string SemVer::to_string() const {
    std::string result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) result += "-" + prerelease;
    if (!build.empty()) result += "+" + build;
    return result;
}

bool SemVer::operator==(const SemVer& other) const {
    return major == other.major && minor == other.minor && patch == other.patch
        && prerelease == other.prerelease;
}

bool SemVer::operator<(const SemVer& other) const {
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    if (patch != other.patch) return patch < other.patch;

    // Prerelease comparison: 1.0.0 > 1.0.0-alpha
    if (prerelease.empty() && !other.prerelease.empty()) return false;
    if (!prerelease.empty() && other.prerelease.empty()) return true;
    if (prerelease != other.prerelease) return prerelease < other.prerelease;
    return false;
}

bool SemVer::satisfies(const std::string& constraint) const {
    std::string c = constraint;
    // Remove whitespace
    c.erase(std::remove_if(c.begin(), c.end(), ::isspace), c.end());

    if (c.empty() || c == "*") return true;

    // Exact version
    if (c[0] != '^' && c[0] != '~' && c[0] != '>' && c[0] != '<' && c[0] != '=') {
        SemVer exact(c);
        return *this == exact;
    }

    // Caret: ^1.2.3 -> >=1.2.3 <2.0.0
    if (c[0] == '^') {
        SemVer base(c.substr(1));
        return satisfies_caret(base);
    }

    // Tilde: ~1.2.3 -> >=1.2.3 <1.3.0
    if (c[0] == '~') {
        SemVer base(c.substr(1));
        return satisfies_tilde(base);
    }

    // Range: >=1.0.0, <2.0.0, >=1.0.0 <2.0.0
    std::regex range_regex(R"((>=|<=|>|<|=)(\d+(?:\.\d+){0,2}(?:-[a-zA-Z0-9.]+)?))");
    std::sregex_iterator it(c.begin(), c.end(), range_regex);
    std::sregex_iterator end;

    SemVer low(0, 0, 0);
    bool has_low = false, low_inc = true;
    SemVer high(99999, 99999, 99999);
    bool has_high = false, high_inc = true;

    while (it != end) {
        std::smatch m = *it;
        std::string op = m[1].str();
        SemVer ver(m[2].str());

        if (op == ">=") {
            if (!has_low || ver > low) { low = ver; has_low = true; low_inc = true; }
        } else if (op == ">") {
            if (!has_low || ver >= low) { low = ver; has_low = true; low_inc = false; }
        } else if (op == "<=") {
            if (!has_high || ver < high) { high = ver; has_high = true; high_inc = true; }
        } else if (op == "<") {
            if (!has_high || ver <= high) { high = ver; has_high = true; high_inc = false; }
        } else if (op == "=") {
            return *this == ver;
        }
        ++it;
    }

    return satisfies_range(low, low_inc, high, high_inc);
}

bool SemVer::satisfies_caret(const SemVer& base) const {
    if (*this < base) return false;
    if (base.major == 0) {
        if (base.minor == 0) {
            return major == 0 && minor == 0 && patch >= base.patch;
        }
        return major == 0 && minor == base.minor && patch >= base.patch;
    }
    return major == base.major;
}

bool SemVer::satisfies_tilde(const SemVer& base) const {
    if (*this < base) return false;
    return major == base.major && minor == base.minor;
}

bool SemVer::satisfies_range(const SemVer& low, bool low_inc,
                              const SemVer& high, bool high_inc) const {
    bool above_low = low_inc ? (*this >= low) : (*this > low);
    bool below_high = high_inc ? (*this <= high) : (*this < high);
    return above_low && below_high;
}

// ============================================================================
// Manifest Implementation
// ============================================================================

std::vector<Dependency> Manifest::get_dependencies(DependencyKind kind) const {
    std::vector<Dependency> result;
    for (const auto& dep : dependencies) {
        if (dep.kind == kind) result.push_back(dep);
    }
    return result;
}

bool Manifest::has_feature(const std::string& name) const {
    return features.find(name) != features.end();
}

std::vector<std::string> Manifest::get_enabled_features(const std::vector<std::string>& extra) const {
    std::vector<std::string> result = default_features;
    for (const auto& f : extra) {
        if (std::find(result.begin(), result.end(), f) == result.end()) {
            result.push_back(f);
        }
    }
    return result;
}

// ============================================================================
// ManifestParser Implementation
// ============================================================================

ManifestParser::ManifestParser() = default;

Manifest ManifestParser::parse_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Manifest manifest;
        manifest.parse_errors.push_back("Cannot open manifest file: " + path.string());
        return manifest;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_string(buffer.str(), path.string());
}

Manifest ManifestParser::parse_string(const std::string& content, const std::string& source_name) {
    errors_.clear();
    auto root = parse_toml(content, source_name);

    Manifest manifest;
    if (!root || root->type != ManifestParser::TomlValue::Type::Table) {
        manifest.parse_errors.push_back("Invalid manifest: expected TOML table at root");
        return manifest;
    }

    manifest = build_manifest(root->table, std::filesystem::path(source_name));
    manifest.parsed = manifest.parse_errors.empty();
    return manifest;
}

bool ManifestParser::is_valid_manifest(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) return false;
    std::string filename = path.filename().string();
    return filename == "Claw.toml" || filename == "claw.toml" ||
           filename == "claw.json" || filename == "Claw.json";
}

bool ManifestParser::write_manifest(const std::filesystem::path& path, const Manifest& manifest) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "[package]\n";
    file << "name = \"" << manifest.package.name << "\"\n";
    file << "version = \"" << manifest.package.version.to_string() << "\"\n";
    if (!manifest.package.description.empty())
        file << "description = \"" << manifest.package.description << "\"\n";
    if (!manifest.package.authors.empty()) {
        file << "authors = [\n";
        for (const auto& a : manifest.package.authors)
            file << "  \"" << a << "\",\n";
        file << "]\n";
    }
    if (!manifest.package.license.empty())
        file << "license = \"" << manifest.package.license << "\"\n";
    if (!manifest.package.repository.empty())
        file << "repository = \"" << manifest.package.repository << "\"\n";
    if (!manifest.package.edition.empty())
        file << "edition = \"" << manifest.package.edition << "\"\n";

    // Dependencies
    if (!manifest.dependencies.empty()) {
        file << "\n[dependencies]\n";
        for (const auto& dep : manifest.dependencies) {
            file << dep.name << " = \"" << dep.version_constraint << "\"\n";
        }
    }

    // Dev dependencies
    if (!manifest.dev_dependencies.empty()) {
        file << "\n[dev-dependencies]\n";
        for (const auto& dep : manifest.dev_dependencies) {
            file << dep.name << " = \"" << dep.version_constraint << "\"\n";
        }
    }

    // Features
    if (!manifest.features.empty()) {
        file << "\n[features]\n";
        for (const auto& [name, deps] : manifest.features) {
            file << name << " = [\n";
            for (const auto& d : deps)
                file << "  \"" << d << "\",\n";
            file << "]\n";
        }
    }

    return true;
}

// ============================================================================
// TOML Parsing (Simplified)
// ============================================================================

std::shared_ptr<ManifestParser::TomlValue> ManifestParser::parse_toml(
    const std::string& content, const std::string& source_name) {

    std::istringstream stream(content);
    TomlTable root;
    TomlTable* current_table = &root;
    std::string line;
    int line_num = 0;

    while (std::getline(stream, line)) {
        line_num++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Table header: [table] or [table.subtable]
        if (line[0] == '[' && line.back() == ']') {
            std::string table_name = line.substr(1, line.size() - 2);
            // Split on '.' for table paths
            std::vector<std::string> keys;
            std::string current_key;
            for (char c : table_name) {
                if (c == '.') {
                    if (!current_key.empty()) keys.push_back(current_key);
                    current_key.clear();
                } else {
                    current_key += c;
                }
            }
            if (!current_key.empty()) keys.push_back(current_key);

            current_table = &root;
            for (const auto& key : keys) {
                auto it = current_table->find(key);
                if (it == current_table->end()) {
                    auto new_table = std::make_shared<TomlValue>(TomlTable{});
                    (*current_table)[key] = new_table;
                    current_table = &new_table->table;
                } else {
                    if (it->second->type != TomlValue::Type::Table) {
                        report_error("Line " + std::to_string(line_num) + ": " + key + " is not a table");
                        return nullptr;
                    }
                    current_table = &it->second->table;
                }
            }
            continue;
        }

        // Key-value pair
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            report_error("Line " + std::to_string(line_num) + ": expected '='");
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value_str = trim(line.substr(eq_pos + 1));

        auto value = parse_toml_scalar(value_str);
        if (!value) {
            // Try array
            if (value_str[0] == '[') {
                std::istringstream arr_stream(value_str);
                value = parse_toml_array(arr_stream);
            }
        }

        if (value) {
            (*current_table)[key] = value;
        }
    }

    return std::make_shared<TomlValue>(std::move(root));
}

std::shared_ptr<ManifestParser::TomlValue> ManifestParser::parse_toml_scalar(const std::string& raw) {
    std::string s = raw;

    // Boolean
    if (s == "true") return std::make_shared<TomlValue>(TomlScalar(true));
    if (s == "false") return std::make_shared<TomlValue>(TomlScalar(false));

    // String
    if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
        return std::make_shared<TomlValue>(TomlScalar(s.substr(1, s.size() - 2)));
    }

    // Integer
    bool is_int = !s.empty() && (s[0] == '-' || std::isdigit(s[0]));
    for (size_t i = 1; i < s.size() && is_int; i++) {
        if (!std::isdigit(s[i])) is_int = false;
    }
    if (is_int) {
        try {
            return std::make_shared<TomlValue>(TomlScalar(static_cast<int64_t>(std::stoll(s))));
        } catch (...) {}
    }

    // Float
    bool is_float = false;
    try {
        size_t pos = 0;
        double d = std::stod(s, &pos);
        if (pos == s.size()) {
            return std::make_shared<TomlValue>(TomlScalar(d));
        }
    } catch (...) {}

    return nullptr;
}

std::shared_ptr<ManifestParser::TomlValue> ManifestParser::parse_toml_array(std::istringstream& stream) {
    TomlArray result;
    std::string token;
    char c;
    bool in_brackets = false;
    std::string current;

    while (stream.get(c)) {
        if (c == '[' && current.empty()) {
            in_brackets = true;
            continue;
        }
        if (c == ']' && in_brackets) {
            if (!current.empty()) {
                auto val = parse_toml_scalar(trim(current));
                if (val) result.push_back(val);
            }
            break;
        }
        if (c == ',' && in_brackets) {
            if (!current.empty()) {
                auto val = parse_toml_scalar(trim(current));
                if (val) result.push_back(val);
                current.clear();
            }
            continue;
        }
        current += c;
    }

    if (!current.empty()) {
        auto val = parse_toml_scalar(trim(current));
        if (val) result.push_back(val);
    }

    return std::make_shared<TomlValue>(std::move(result));
}

// ============================================================================
// Manifest Building
// ============================================================================

Manifest ManifestParser::build_manifest(const TomlTable& root, const std::filesystem::path& path) {
    Manifest manifest;
    manifest.manifest_path = path;

    // Parse [package]
    auto pkg_it = root.find("package");
    if (pkg_it == root.end() || pkg_it->second->type != TomlValue::Type::Table) {
        manifest.parse_errors.push_back("Missing [package] section");
        return manifest;
    }
    manifest.package = parse_package_metadata(pkg_it->second->table);

    // Parse [dependencies]
    auto deps_it = root.find("dependencies");
    if (deps_it != root.end() && deps_it->second->type == TomlValue::Type::Table) {
        manifest.dependencies = parse_dependencies(deps_it->second->table, DependencyKind::Normal);
    }

    // Parse [dev-dependencies]
    auto dev_deps_it = root.find("dev-dependencies");
    if (dev_deps_it == root.end()) dev_deps_it = root.find("dev_dependencies");
    if (dev_deps_it != root.end() && dev_deps_it->second->type == TomlValue::Type::Table) {
        manifest.dev_dependencies = parse_dependencies(dev_deps_it->second->table, DependencyKind::Dev);
    }

    // Parse [build-dependencies]
    auto build_deps_it = root.find("build-dependencies");
    if (build_deps_it == root.end()) build_deps_it = root.find("build_dependencies");
    if (build_deps_it != root.end() && build_deps_it->second->type == TomlValue::Type::Table) {
        manifest.build_dependencies = parse_dependencies(build_deps_it->second->table, DependencyKind::Build);
    }

    // Parse [features]
    auto features_it = root.find("features");
    if (features_it != root.end() && features_it->second->type == TomlValue::Type::Table) {
        manifest.features = parse_features(features_it->second->table);
    }

    return manifest;
}

PackageMetadata ManifestParser::parse_package_metadata(const TomlTable& pkg_table) {
    PackageMetadata pkg;

    auto get_string = [&](const std::string& key) -> std::string {
        auto it = pkg_table.find(key);
        if (it != pkg_table.end() && it->second->type == TomlValue::Type::Scalar) {
            if (auto* s = std::get_if<std::string>(&it->second->scalar)) return *s;
        }
        return "";
    };

    pkg.name = get_string("name");
    pkg.version = SemVer(get_string("version"));
    pkg.description = get_string("description");
    pkg.license = get_string("license");
    pkg.repository = get_string("repository");
    pkg.homepage = get_string("homepage");
    pkg.documentation = get_string("documentation");
    pkg.readme = get_string("readme");
    pkg.edition = get_string("edition");
    if (pkg.edition.empty()) pkg.edition = "2026";

    // Authors array
    auto authors_it = pkg_table.find("authors");
    if (authors_it != pkg_table.end() && authors_it->second->type == TomlValue::Type::Array) {
        for (const auto& val : authors_it->second->array) {
            if (val->type == TomlValue::Type::Scalar) {
                if (auto* s = std::get_if<std::string>(&val->scalar)) {
                    pkg.authors.push_back(*s);
                }
            }
        }
    }

    // Keywords array
    auto keywords_it = pkg_table.find("keywords");
    if (keywords_it != pkg_table.end() && keywords_it->second->type == TomlValue::Type::Array) {
        for (const auto& val : keywords_it->second->array) {
            if (val->type == TomlValue::Type::Scalar) {
                if (auto* s = std::get_if<std::string>(&val->scalar)) {
                    pkg.keywords.push_back(*s);
                }
            }
        }
    }

    return pkg;
}

std::vector<Dependency> ManifestParser::parse_dependencies(
    const TomlTable& deps_table, DependencyKind kind) {

    std::vector<Dependency> result;
    for (const auto& [name, value] : deps_table) {
        result.push_back(parse_dependency_entry(name, value));
        result.back().kind = kind;
    }
    return result;
}

Dependency ManifestParser::parse_dependency_entry(
    const std::string& name, const std::shared_ptr<TomlValue>& value) {

    Dependency dep(name);

    if (value->type == TomlValue::Type::Scalar) {
        if (auto* s = std::get_if<std::string>(&value->scalar)) {
            dep.version_constraint = *s;
        }
    } else if (value->type == TomlValue::Type::Table) {
        auto& tbl = value->table;

        auto ver_it = tbl.find("version");
        if (ver_it != tbl.end() && ver_it->second->type == TomlValue::Type::Scalar) {
            if (auto* s = std::get_if<std::string>(&ver_it->second->scalar))
                dep.version_constraint = *s;
        }

        auto path_it = tbl.find("path");
        if (path_it != tbl.end() && path_it->second->type == TomlValue::Type::Scalar) {
            if (auto* s = std::get_if<std::string>(&path_it->second->scalar))
                dep.version_constraint = "path:" + *s;
        }

        auto git_it = tbl.find("git");
        if (git_it != tbl.end() && git_it->second->type == TomlValue::Type::Scalar) {
            if (auto* s = std::get_if<std::string>(&git_it->second->scalar))
                dep.version_constraint = "git:" + *s;
        }

        auto reg_it = tbl.find("registry");
        if (reg_it != tbl.end() && reg_it->second->type == TomlValue::Type::Scalar) {
            if (auto* s = std::get_if<std::string>(&reg_it->second->scalar))
                dep.registry = *s;
        }

        auto opt_it = tbl.find("optional");
        if (opt_it != tbl.end() && opt_it->second->type == TomlValue::Type::Scalar) {
            if (auto* b = std::get_if<bool>(&opt_it->second->scalar))
                dep.optional = *b;
        }

        // Features
        auto feat_it = tbl.find("features");
        if (feat_it != tbl.end() && feat_it->second->type == TomlValue::Type::Array) {
            for (const auto& f : feat_it->second->array) {
                if (f->type == TomlValue::Type::Scalar) {
                    if (auto* s = std::get_if<std::string>(&f->scalar))
                        dep.features.push_back(*s);
                }
            }
        }
    }

    return dep;
}

std::unordered_map<std::string, std::vector<std::string>> ManifestParser::parse_features(
    const TomlTable& features_table) {

    std::unordered_map<std::string, std::vector<std::string>> result;
    for (const auto& [name, value] : features_table) {
        if (value->type == TomlValue::Type::Array) {
            for (const auto& item : value->array) {
                if (item->type == TomlValue::Type::Scalar) {
                    if (auto* s = std::get_if<std::string>(&item->scalar)) {
                        result[name].push_back(*s);
                    }
                }
            }
        }
    }
    return result;
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string ManifestParser::trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start])) start++;
    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1])) end--;
    return s.substr(start, end - start);
}

std::vector<std::string> ManifestParser::split_lines(const std::string& s) const {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

void ManifestParser::report_error(const std::string& msg) {
    errors_.push_back(msg);
}

// ============================================================================
// ManifestDiscovery Implementation
// ============================================================================

std::optional<std::filesystem::path> ManifestDiscovery::find_manifest(
    const std::filesystem::path& start_path) {

    auto current = std::filesystem::absolute(start_path);
    if (std::filesystem::is_regular_file(current)) {
        current = current.parent_path();
    }

    while (true) {
        for (const auto& name : {"Claw.toml", "claw.toml"}) {
            auto candidate = current / name;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }

        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> ManifestDiscovery::find_workspace_manifests(
    const Manifest& root_manifest) {

    std::vector<std::filesystem::path> result;
    if (!root_manifest.workspace) return result;

    auto base_dir = root_manifest.manifest_path.parent_path();
    for (const auto& member : root_manifest.workspace->members) {
        auto member_path = base_dir / member.path / "Claw.toml";
        if (std::filesystem::exists(member_path)) {
            result.push_back(member_path);
        }
    }
    return result;
}

std::filesystem::path ManifestDiscovery::module_id_to_manifest_path(
    const std::string& module_id, const std::filesystem::path& search_root) {

    std::string path_str;
    for (size_t i = 0; i < module_id.size(); i++) {
        if (i + 1 < module_id.size() && module_id[i] == ':' && module_id[i + 1] == ':') {
            path_str += '/';
            i++;
        } else {
            path_str += module_id[i];
        }
    }
    return search_root / path_str / "Claw.toml";
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string dependency_kind_to_string(DependencyKind kind) {
    switch (kind) {
        case DependencyKind::Normal: return "normal";
        case DependencyKind::Dev: return "dev";
        case DependencyKind::Build: return "build";
        case DependencyKind::Optional: return "optional";
        default: return "unknown";
    }
}

DependencyKind string_to_dependency_kind(const std::string& s) {
    if (s == "dev") return DependencyKind::Dev;
    if (s == "build") return DependencyKind::Build;
    if (s == "optional") return DependencyKind::Optional;
    return DependencyKind::Normal;
}

std::string target_kind_to_string(TargetKind kind) {
    switch (kind) {
        case TargetKind::Binary: return "bin";
        case TargetKind::Library: return "lib";
        case TargetKind::Test: return "test";
        case TargetKind::Benchmark: return "bench";
        case TargetKind::Example: return "example";
        default: return "unknown";
    }
}

} // namespace package
} // namespace claw
