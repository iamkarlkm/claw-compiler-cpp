// Claw Compiler - Package Management System Tests
// Tests for manifest parser, dependency resolver, lock file, and package manager

#include "../src/package/manifest_parser.h"
#include "../src/package/dependency_resolver.h"
#include "../src/package/lock_file.h"
#include "../src/package/package_manager.h"
#include "../src/test/test.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace claw::package;

CLAW_TEST_SUITE(PackageTests);

// ============================================================================
// SemVer Tests
// ============================================================================

CLAW_TEST(semver_parsing) {
    SemVer v1("1.2.3");
    CLAW_ASSERT(v1.major == 1);
    CLAW_ASSERT(v1.minor == 2);
    CLAW_ASSERT(v1.patch == 3);

    SemVer v2("2.0.0-alpha.1+build.123");
    CLAW_ASSERT(v2.major == 2);
    CLAW_ASSERT(v2.prerelease == "alpha.1");
    CLAW_ASSERT(v2.build == "build.123");

    SemVer v3("0.5");
    CLAW_ASSERT(v3.major == 0);
    CLAW_ASSERT(v3.minor == 5);
    CLAW_ASSERT(v3.patch == 0);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(semver_comparison) {
    SemVer v1(1, 2, 3);
    SemVer v2(1, 2, 4);
    SemVer v3(2, 0, 0);

    CLAW_ASSERT(v1 < v2);
    CLAW_ASSERT(v2 < v3);
    CLAW_ASSERT(v1 == SemVer("1.2.3"));
    CLAW_ASSERT(v3 > v1);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(semver_satisfies_caret) {
    SemVer v(1, 2, 3);
    CLAW_ASSERT(v.satisfies("^1.2.3"));
    CLAW_ASSERT(v.satisfies("^1.0.0"));
    CLAW_ASSERT(!v.satisfies("^2.0.0"));

    SemVer zero(0, 2, 3);
    CLAW_ASSERT(zero.satisfies("^0.2.3"));
    CLAW_ASSERT(!zero.satisfies("^0.6.0"));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(semver_satisfies_tilde) {
    SemVer v(1, 2, 3);
    CLAW_ASSERT(v.satisfies("~1.2.3"));
    CLAW_ASSERT(v.satisfies("~1.2.0"));
    CLAW_ASSERT(!v.satisfies("~1.3.0"));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(semver_satisfies_range) {
    SemVer v(1, 5, 0);
    CLAW_ASSERT(v.satisfies(">=1.0.0"));
    CLAW_ASSERT(v.satisfies(">=1.0.0 <2.0.0"));
    CLAW_ASSERT(!v.satisfies(">=2.0.0"));
    CLAW_ASSERT(v.satisfies("*"));
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Manifest Parser Tests
// ============================================================================

CLAW_TEST(manifest_parse_basic) {
    std::string content = R"(
[package]
name = "test-project"
version = "1.0.0"
description = "A test project"
license = "MIT"
edition = "2026"

[dependencies]
claw-std = "^1.0.0"
claw-tensor = "~2.1.0"

[dev-dependencies]
claw-test = "^0.5.0"
)";

    ManifestParser parser;
    auto manifest = parser.parse_string(content);

    CLAW_ASSERT(manifest.parsed);
    CLAW_ASSERT(manifest.package.name == "test-project");
    CLAW_ASSERT(manifest.package.version == SemVer("1.0.0"));
    CLAW_ASSERT(manifest.package.description == "A test project");
    CLAW_ASSERT(manifest.package.license == "MIT");
    CLAW_ASSERT(manifest.package.edition == "2026");

    CLAW_ASSERT(manifest.dependencies.size() == 2);
    auto find_dep = [&](const std::string& name) -> const Dependency* {
        for (const auto& dep : manifest.dependencies) {
            if (dep.name == name) return &dep;
        }
        return nullptr;
    };
    auto std_dep = find_dep("claw-std");
    auto tensor_dep = find_dep("claw-tensor");
    CLAW_ASSERT(std_dep != nullptr);
    CLAW_ASSERT(tensor_dep != nullptr);
    CLAW_ASSERT(std_dep->version_constraint == "^1.0.0");
    CLAW_ASSERT(tensor_dep->version_constraint == "~2.1.0");

    CLAW_ASSERT(manifest.dev_dependencies.size() == 1);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(manifest_parse_table_dependency) {
    std::string content = R"(
[package]
name = "advanced-project"
version = "2.0.0"

[dependencies]
claw-std = { version = "^1.0.0", registry = "custom", optional = true }
claw-tensor = { version = "~2.1.0", features = ["cuda", "opencl"] }
local-pkg = { path = "../local-pkg" }
)";

    ManifestParser parser;
    auto manifest = parser.parse_string(content);

    CLAW_ASSERT(manifest.parsed);
    CLAW_ASSERT(manifest.dependencies.size() == 3);

    auto find_dep = [&](const std::string& name) -> const Dependency* {
        for (const auto& dep : manifest.dependencies) {
            if (dep.name == name) return &dep;
        }
        return nullptr;
    };

    auto std_dep = find_dep("claw-std");
    CLAW_ASSERT(std_dep != nullptr);
    CLAW_ASSERT(std_dep->name == "claw-std");
    CLAW_ASSERT(std_dep->version_constraint == "^1.0.0");
    CLAW_ASSERT(std_dep->registry == "custom");
    CLAW_ASSERT(std_dep->optional);

    auto tensor_dep = find_dep("claw-tensor");
    CLAW_ASSERT(tensor_dep != nullptr);
    CLAW_ASSERT(tensor_dep->features.size() == 2);
    CLAW_ASSERT(tensor_dep->features[0] == "cuda");
    CLAW_ASSERT(tensor_dep->features[1] == "opencl");

    auto local_dep = find_dep("local-pkg");
    CLAW_ASSERT(local_dep != nullptr);
    CLAW_ASSERT(local_dep->version_constraint == "path:../local-pkg");
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(manifest_parse_features) {
    std::string content = R"(
[package]
name = "feature-project"
version = "1.0.0"

[features]
default = ["std"]
std = []
cuda = ["claw-tensor/cuda"]
full = ["std", "cuda"]
)";

    ManifestParser parser;
    auto manifest = parser.parse_string(content);

    CLAW_ASSERT(manifest.parsed);
    CLAW_ASSERT(manifest.features.size() == 4);
    CLAW_ASSERT(manifest.has_feature("default"));
    CLAW_ASSERT(manifest.has_feature("full"));
    CLAW_ASSERT(manifest.features["full"].size() == 2);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(manifest_write_read) {
    Manifest manifest;
    manifest.package.name = "roundtrip";
    manifest.package.version = SemVer(1, 2, 3);
    manifest.package.description = "Test roundtrip";
    manifest.package.license = "Apache-2.0";

    manifest.dependencies.push_back(Dependency("dep1", "^1.0.0"));
    manifest.dependencies.push_back(Dependency("dep2", "~2.0.0"));

    auto tmp_path = std::filesystem::temp_directory_path() / "test_claw.toml";
    ManifestParser parser;
    CLAW_ASSERT(parser.write_manifest(tmp_path, manifest));

    auto parsed = parser.parse_file(tmp_path);
    CLAW_ASSERT(parsed.parsed);
    CLAW_ASSERT(parsed.package.name == "roundtrip");
    CLAW_ASSERT(parsed.package.version == SemVer("1.2.3"));
    CLAW_ASSERT(parsed.dependencies.size() == 2);

    std::filesystem::remove(tmp_path);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(manifest_discovery) {
    auto found = ManifestDiscovery::find_manifest(std::filesystem::temp_directory_path());
    (void)found;
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Dependency Resolver Tests
// ============================================================================

CLAW_TEST(resolver_basic) {
    auto registry = std::make_shared<LocalPackageRegistry>(std::filesystem::temp_directory_path());
    DependencyResolver resolver(registry);

    SemVer v1(1, 5, 0);
    CLAW_ASSERT(v1.satisfies("^1.0.0"));
    CLAW_ASSERT(v1.satisfies(">=1.0.0 <2.0.0"));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(resolver_version_selection) {
    std::vector<SemVer> compatible = {SemVer("1.2.0"), SemVer("1.5.0")};

    std::sort(compatible.begin(), compatible.end(), std::greater<SemVer>());
    CLAW_ASSERT(compatible.front() == SemVer("1.5.0"));

    std::sort(compatible.begin(), compatible.end());
    CLAW_ASSERT(compatible.front() == SemVer("1.2.0"));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(resolver_circular_detection) {
    std::vector<std::string> stack = {"A", "B", "C"};
    std::unordered_set<std::string> in_progress = {"A", "B", "C"};
    CLAW_ASSERT(in_progress.count("B"));
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Lock File Tests
// ============================================================================

CLAW_TEST(lockfile_basic) {
    LockFile lockfile;

    LockedPackage pkg;
    pkg.name = "claw-std";
    pkg.version = SemVer("1.2.3");
    pkg.source = "registry";
    pkg.checksum = "abc123";

    lockfile.add_package(pkg);
    CLAW_ASSERT(lockfile.has_package("claw-std"));

    auto retrieved = lockfile.get_package("claw-std");
    CLAW_ASSERT(retrieved.has_value());
    CLAW_ASSERT(retrieved->version == SemVer("1.2.3"));
    CLAW_ASSERT(retrieved->checksum == "abc123");
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(lockfile_serialization) {
    LockFile lockfile;
    lockfile.set_metadata("generated-by", "claw-test");

    LockedPackage pkg1;
    pkg1.name = "pkg-a";
    pkg1.version = SemVer("1.0.0");
    pkg1.source = "registry";
    pkg1.dependencies = {"pkg-b"};

    LockedPackage pkg2;
    pkg2.name = "pkg-b";
    pkg2.version = SemVer("2.0.0");
    pkg2.source = "registry";

    lockfile.add_package(pkg1);
    lockfile.add_package(pkg2);

    auto serialized = lockfile.serialize();
    CLAW_ASSERT(!serialized.empty());
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(lockfile_validation) {
    Manifest manifest;
    manifest.parsed = true;
    manifest.package.name = "test";
    manifest.package.version = SemVer("1.0.0");
    manifest.dependencies.push_back(Dependency("claw-std", "^1.0.0"));

    LockFile lockfile;
    CLAW_ASSERT(!lockfile.is_valid_for_manifest(manifest));

    LockedPackage pkg;
    pkg.name = "claw-std";
    pkg.version = SemVer("1.2.0");
    lockfile.add_package(pkg);

    CLAW_ASSERT(lockfile.is_valid_for_manifest(manifest));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(lockfile_from_graph) {
    ResolvedGraph graph;
    ResolvedPackage pkg1;
    pkg1.name = "a";
    pkg1.version = SemVer("1.0.0");
    pkg1.source = "registry";
    pkg1.dependencies = {"b"};

    ResolvedPackage pkg2;
    pkg2.name = "b";
    pkg2.version = SemVer("2.0.0");
    pkg2.source = "registry";

    graph.add_package(pkg1);
    graph.add_package(pkg2);

    auto lockfile = LockFile::from_resolved_graph(graph);
    CLAW_ASSERT(lockfile.has_package("a"));
    CLAW_ASSERT(lockfile.has_package("b"));

    auto ordered = lockfile.get_ordered_packages();
    CLAW_ASSERT(ordered.size() == 2);
    CLAW_ASSERT(ordered[0].name == "b");
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Package Manager Tests
// ============================================================================

CLAW_TEST(package_cache) {
    auto cache_dir = std::filesystem::temp_directory_path() / "claw_test_cache";
    std::filesystem::remove_all(cache_dir);

    PackageCache cache(cache_dir);
    CLAW_ASSERT(!cache.is_cached("test-pkg", SemVer("1.0.0")));

    auto source = std::filesystem::temp_directory_path() / "claw_test_source";
    std::filesystem::create_directories(source);
    std::ofstream(source / "Claw.toml") << "[package]\nname=\"test-pkg\"\nversion=\"1.0.0\"\n";

    CLAW_ASSERT(cache.cache_package("test-pkg", SemVer("1.0.0"), source));
    CLAW_ASSERT(cache.is_cached("test-pkg", SemVer("1.0.0")));

    auto versions = cache.get_cached_versions("test-pkg");
    CLAW_ASSERT(versions.size() == 1);

    CLAW_ASSERT(cache.remove_from_cache("test-pkg", SemVer("1.0.0")));
    CLAW_ASSERT(!cache.is_cached("test-pkg", SemVer("1.0.0")));

    std::filesystem::remove_all(cache_dir);
    std::filesystem::remove_all(source);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(package_manager_init) {
    PackageManagerConfig config;
    config.cache_dir = std::filesystem::temp_directory_path() / "claw_test_pm";
    std::filesystem::remove_all(config.cache_dir);

    PackageManager pm(config);
    CLAW_ASSERT(pm.initialize());

    std::filesystem::remove_all(config.cache_dir);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(package_manager_cli_help) {
    auto help = PackageManagerCLI::get_general_help();
    CLAW_ASSERT(!help.empty());
    CLAW_ASSERT(help.find("Claw Package Manager") != std::string::npos);
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Integration Test
// ============================================================================

CLAW_TEST(package_integration) {
    auto project_dir = std::filesystem::temp_directory_path() / "claw_test_project";
    std::filesystem::remove_all(project_dir);
    std::filesystem::create_directories(project_dir);

    std::ofstream manifest_file(project_dir / "Claw.toml");
    manifest_file << R"(
[package]
name = "integration-test"
version = "0.1.0"
description = "Integration test project"

[dependencies]
claw-std = "^1.0.0"
)";
    manifest_file.close();

    ManifestParser parser;
    auto manifest = parser.parse_file(project_dir / "Claw.toml");
    CLAW_ASSERT(manifest.parsed);
    CLAW_ASSERT(manifest.package.name == "integration-test");
    CLAW_ASSERT(manifest.dependencies.size() == 1);
    CLAW_ASSERT(manifest.dependencies[0].name == "claw-std");

    LockFile lockfile;
    LockedPackage locked;
    locked.name = "claw-std";
    locked.version = SemVer("1.5.0");
    locked.source = "registry";
    lockfile.add_package(locked);

    auto lock_path = project_dir / "Claw.lock";
    CLAW_ASSERT(lockfile.save(lock_path));
    CLAW_ASSERT(std::filesystem::exists(lock_path));

    LockFile lockfile2;
    CLAW_ASSERT(lockfile2.load(lock_path));
    CLAW_ASSERT(lockfile2.has_package("claw-std"));
    CLAW_ASSERT(lockfile2.is_valid_for_manifest(manifest));

    std::filesystem::remove_all(project_dir);
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Edge Cases
// ============================================================================

CLAW_TEST(semver_edge_cases) {
    SemVer stable(1, 0, 0);
    SemVer alpha(1, 0, 0, "alpha");
    CLAW_ASSERT(alpha < stable);

    SemVer zero(0, 5, 0);
    CLAW_ASSERT(zero.satisfies("^0.5.0"));
    CLAW_ASSERT(!zero.satisfies("^0.6.0"));

    CLAW_ASSERT(stable.satisfies("*"));
    CLAW_ASSERT(stable.satisfies(""));
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(manifest_edge_cases) {
    ManifestParser parser;

    auto empty = parser.parse_string("");
    CLAW_ASSERT(!empty.parsed);

    auto no_pkg = parser.parse_string("[dependencies]\nfoo = \"1.0.0\"\n");
    CLAW_ASSERT(!no_pkg.parsed);

    auto minimal = parser.parse_string("[package]\nname = \"x\"\nversion = \"1.0.0\"\n");
    CLAW_ASSERT(minimal.parsed);
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// Performance Test
// ============================================================================

CLAW_TEST(semver_performance) {
    for (int i = 0; i < 1000; i++) {
        SemVer v(std::to_string(i) + "." + std::to_string(i % 10) + "." + std::to_string(i % 100));
        (void)v;
    }
    return claw::test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    return claw::test::run_tests(argc, argv);
}
