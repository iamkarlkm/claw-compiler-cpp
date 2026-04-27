#include "../src/package/manifest_parser.h"
#include <iostream>
using namespace claw::package;

int main() {
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
    
    std::cout << "Parsed: " << manifest.parsed << std::endl;
    std::cout << "Name: " << manifest.package.name << std::endl;
    std::cout << "Version: " << manifest.package.version.to_string() << std::endl;
    std::cout << "Deps: " << manifest.dependencies.size() << std::endl;
    std::cout << "DevDeps: " << manifest.dev_dependencies.size() << std::endl;
    
    for (const auto& err : manifest.parse_errors) {
        std::cout << "Error: " << err << std::endl;
    }
    
    return 0;
}
