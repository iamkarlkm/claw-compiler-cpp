# Claw Compiler - CMake 构建配置

cmake_minimum_required(VERSION 3.16)
project(ClawCompiler VERSION 0.1.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找 LLVM (可选)
find_package(LLVM 14 QUIET)
if(LLVM_FOUND)
    message(STATUS "LLVM found: ${LLVM_PACKAGE_VERSION}")
    include_directories(${LLVM_INCLUDE_DIRS})
    link_directories(${LLVM_LIBRARY_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
else()
    message(STATUS "LLVM not found - code generation will be limited")
endif()

# 头文件目录
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/lexer
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/ast
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/interpreter
    ${CMAKE_SOURCE_DIR}/src/codegen
    ${CMAKE_SOURCE_DIR}/src/ir
    ${CMAKE_SOURCE_DIR}/src/type
    ${CMAKE_SOURCE_DIR}/src/semantic
    ${CMAKE_SOURCE_DIR}/src/tensorir
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/test
)

# 主要源文件 (所有实现都在头文件中内联)
set(CLAW_SOURCES
    src/main.cpp
)

# 可执行文件: claw (完整版)
add_executable(claw ${CLAW_SOURCES})

# 可执行文件: claw-llvm (带 LLVM 代码生成)
if(LLVM_FOUND)
    add_executable(claw-llvm 
        src/main.cpp
        src/codegen/codegen.cpp
    )
    target_link_libraries(claw-llvm
        ${LLVM_LIBS}
        ncurses
    )
endif()

# 可执行文件: claw-opt (优化器)
add_executable(claw-opt
    src/optimizer.cpp
    src/const_fold.cpp
)

# 测试可执行文件
enable_testing()
add_executable(claw-tests
    src/test/run_tests.cpp
    src/test/test_lexer.cpp
    src/test/test_parser.cpp
)
target_link_libraries(claw-tests pthread)
add_test(NAME claw-tests COMMAND claw-tests)

# 调试构建选项
option(CLAW_DEBUG "Enable debug build" OFF)
if(CLAW_DEBUG)
    add_compile_definitions(CLAW_DEBUG=1)
    target_compile_options(claw PRIVATE -g -O0)
else()
    target_compile_options(claw PRIVATE -O3)
endif()

# 详细编译选项
option(CLAW_VERBOSE "Verbose compilation output" OFF)
if(CLAW_VERBOSE)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()

# 安装目标
install(TARGETS claw claw-opt
    RUNTIME DESTINATION bin
)
