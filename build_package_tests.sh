#!/bin/bash
# Build script for package management system tests

cd ~/Documents/complers/agi-development/claw-compiler

OUTPUT="test_package_manager"

SOURCES=(
    src/test/test_package_manager.cpp
    src/test/test_package_manager_main.cpp
    src/package/manifest_parser.cpp
    src/package/dependency_resolver.cpp
    src/package/lock_file.cpp
    src/package/package_manager.cpp
)

INCLUDES=(
    -I./src
    -I./src/common
    -I./src/package
    -I./src/lexer
    -I./src/parser
    -I./src/ast
    -I./src/test
)

CXX_FLAGS="-std=c++17 -O2 -Wall -Wextra"

echo "Building package management tests..."
clang++ ${CXX_FLAGS} "${INCLUDES[@]}" "${SOURCES[@]}" -o ${OUTPUT}

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Running tests..."
    ./${OUTPUT}
else
    echo "Build failed!"
    exit 1
fi
