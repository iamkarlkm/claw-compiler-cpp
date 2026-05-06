#!/bin/bash
# 编译 JIT 编译器 (包含去优化) 测试

cd /Users/mac/Documents/complers/agi-development/claw-compiler

# 核心源文件
SOURCES="src/jit/jit_compiler.cpp src/jit/deoptimization.cpp src/emitter/x86_64_emitter.cpp"
INCLUDES="-Isrc -I src/jit -I src/bytecode -I src/emitter -I src/lexer -I src/parser -I src/ast -I src/common"
CXX="clang++"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -g -c"

echo "Compiling JIT compiler with deoptimization..."
$CXX $CXXFLAGS $INCLUDES $SOURCES 2>&1 | head -40

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo "Compilation successful!"
else
    echo "Compilation failed!"
    exit 1
fi
