#!/bin/bash
# 编译去优化测试

cd /Users/mac/Documents/complers/agi-development/claw-compiler

SOURCES="src/test/test_deoptimization.cpp"
INCLUDES="-Isrc -I src/jit -I src/bytecode -I src/emitter -I src/lexer -I src/parser -I src/ast -I src/common"
CXX="clang++"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -g"

echo "Compiling deoptimization test..."
$CXX $CXXFLAGS $INCLUDES $SOURCES -o test_deoptimization 2>&1

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Running tests..."
    ./test_deoptimization
else
    echo "Compilation failed!"
    exit 1
fi
