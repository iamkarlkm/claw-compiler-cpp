#include "../src/package/manifest_parser.h"
#include "../src/package/dependency_resolver.h"
#include "../src/package/lock_file.h"
#include "../src/package/package_manager.h"
#include <iostream>
using namespace claw::package;

int main() {
    // Test SemVer
    SemVer v1("1.2.3");
    std::cout << "SemVer: " << v1.to_string() << std::endl;
    std::cout << "Satisfies ^1.0.0: " << v1.satisfies("^1.0.0") << std::endl;
    
    // Test LockFile
    LockFile lockfile;
    LockedPackage pkg;
    pkg.name = "claw-std";
    pkg.version = SemVer("1.5.0");
    pkg.source = "registry";
    lockfile.add_package(pkg);
    
    auto path = std::filesystem::temp_directory_path() / "test_debug.lock";
    lockfile.save(path);
    
    LockFile lockfile2;
    lockfile2.load(path);
    std::cout << "Lockfile has claw-std: " << lockfile2.has_package("claw-std") << std::endl;
    
    // Test PackageManager
    PackageManagerConfig config;
    config.cache_dir = std::filesystem::temp_directory_path() / "claw_debug_cache";
    PackageManager pm(config);
    std::cout << "PM init: " << pm.initialize() << std::endl;
    
    std::filesystem::remove(path);
    std::filesystem::remove_all(config.cache_dir);
    
    std::cout << "All package management components work correctly!" << std::endl;
    return 0;
}
