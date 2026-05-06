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
    ${CMAKE_SOURCE_DIR}/src/jit
    ${CMAKE_SOURCE_DIR}/src/emitter
    ${CMAKE_SOURCE_DIR}/src/bytecode
    ${CMAKE_SOURCE_DIR}/src/vm
    ${CMAKE_SOURCE_DIR}/src/tensor
    ${CMAKE_SOURCE_DIR}/src/module
    ${CMAKE_SOURCE_DIR}/src/lsp
    ${CMAKE_SOURCE_DIR}/src/backend
    ${CMAKE_SOURCE_DIR}/src/benchmark
    ${CMAKE_SOURCE_DIR}/src/pipeline
    ${CMAKE_SOURCE_DIR}/src/package
    ${CMAKE_SOURCE_DIR}/src/debugger
    ${CMAKE_SOURCE_DIR}/src/stdlib
    ${CMAKE_SOURCE_DIR}/src/bridge
    ${CMAKE_SOURCE_DIR}/src/ml
    ${CMAKE_SOURCE_DIR}/src/auto_scheduler
    ${CMAKE_SOURCE_DIR}/src/json
    ${CMAKE_SOURCE_DIR}/src/repl
    ${CMAKE_SOURCE_DIR}/src/native_codegen
)

# 主要源文件
set(CLAW_SOURCES
    src/main.cpp
    src/jit/jit_compiler.cpp
    src/jit/jit_runtime.cpp
    src/jit/jit_stdlib_integration.cpp
    src/module/module.cpp
    src/repl/claw_repl.cpp
    src/repl/claw_repl_integrated.cpp
    src/bytecode/bytecode_compiler.cpp
    src/bytecode/bytecode_executor.cpp
    src/ir_bytecode_bridge.cpp
    src/bridge/ir_bytecode_bridge.cpp
    src/pipeline/execution_engine.cpp
    src/pipeline/execution_engine_enhanced.cpp
    src/pipeline/execution_pipeline.cpp
    src/pipeline/perf_profiler.cpp
    src/debugger/claw_debugger.cpp
    src/debugger/claw_debugger_cli.cpp
    src/stdlib/stdlib.cpp
    src/stdlib/stdlib_bytecode_integration.cpp
    src/package/package_manager.cpp
    src/package/manifest_parser.cpp
    src/package/dependency_resolver.cpp
    src/package/lock_file.cpp
    src/auto_scheduler/auto_scheduler.cpp
    src/auto_scheduler/vm_evaluator.cpp
    src/auto_scheduler/ml_evaluator.cpp
    src/auto_scheduler/schedule_cache.cpp
    src/auto_scheduler/schedule_space.cpp
    src/auto_scheduler/search_strategy.cpp
    src/ml/feature_extractor.cpp
    src/ml/ml_cost_model.cpp
    src/ml/ml_cost_model_adapter.cpp
    src/backend/cuda_codegen.cpp
    src/benchmark/benchmark.cpp
    src/native_codegen/native_codegen.cpp
    src/tensorir/tensor_ir.cpp
    src/tensorir/tensor_ir_generator.cpp
    src/ir/ir_optimizer.cpp
    src/ir/ir.cpp
    src/ir/ir_enhanced.cpp
    src/ir/ir_generator.cpp
    src/ir/ir_generator_enhanced.cpp
    src/bytecode/bytecode.cpp
    src/bytecode/bytecode_compiler_simple.cpp
    src/codegen/native_codegen.cpp
    src/codegen/x86_64_codegen.cpp
    src/emitter/arm64_emitter.cpp
    src/emitter/reg_alloc.cpp
    src/emitter/riscv_emitter.cpp
    src/emitter/x86_64_emitter.cpp
    src/interpreter/interpreter.cpp
    src/semantic/semantic_analyzer.cpp
    src/type/type_checker.cpp
    src/vm/claw_vm.cpp
    src/jit/hot_spot.cpp
    src/jit/inline_cache.cpp
    src/jit/optimizations.cpp
    src/jit/tracing_jit.cpp
    src/jit/type_profiler.cpp
    src/repl/repl.cpp
    src/ir/ir_optimizer.cpp
)

# LSP 服务器源文件
set(CLAW_LSP_SOURCES
    src/lsp/lsp_protocol.cpp
    src/lsp/lsp_server.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
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

# 基础测试
add_executable(claw-tests
    src/test/run_tests.cpp
    src/test/test_lexer.cpp
    src/test/test_parser.cpp
)
target_link_libraries(claw-tests pthread)
add_test(NAME claw-tests COMMAND claw-tests)

# 基准测试框架测试
add_executable(claw-test-benchmark
    src/benchmark/benchmark.cpp
    src/test/test_benchmark.cpp
)
add_test(NAME claw-test-benchmark COMMAND claw-test-benchmark)

# 包管理器测试
add_executable(claw-test-package
    src/package/package_manager.cpp
    src/package/manifest_parser.cpp
    src/package/dependency_resolver.cpp
    src/package/lock_file.cpp
    src/test/test_package_manager.cpp
)
add_test(NAME claw-test-package COMMAND claw-test-package)

# CUDA 代码生成器测试
add_executable(claw-test-cuda
    src/backend/cuda_codegen.cpp
    src/tensorir/tensor_ir.cpp
    src/test/test_cuda_codegen.cpp
)
add_test(NAME claw-test-cuda COMMAND claw-test-cuda)

# 调试器测试
add_executable(claw-test-debugger
    src/debugger/claw_debugger.cpp
    src/test/test_debugger.cpp
)
add_test(NAME claw-test-debugger COMMAND claw-test-debugger)

# 性能分析器测试
add_executable(claw-test-profiler
    src/pipeline/perf_profiler.cpp
    src/test/test_optimizations.cpp
)
add_test(NAME claw-test-profiler COMMAND claw-test-profiler)

# 反优化测试
add_executable(claw-test-deopt
    src/jit/deoptimization.cpp
    src/test/test_deoptimization.cpp
)
add_test(NAME claw-test-deopt COMMAND claw-test-deopt)

# 栈帧测试
add_executable(claw-test-stackframe
    src/jit/stack_frame.cpp
    src/test/test_stack_frame.cpp
)
add_test(NAME claw-test-stackframe COMMAND claw-test-stackframe)

# WASM IR 测试
add_executable(claw-test-wasm
    src/emitter/wasm/wasm_backend.cpp
    src/emitter/wasm/wasm_ir_generator.cpp
    src/test/test_wasm_ir.cpp
)
add_test(NAME claw-test-wasm COMMAND claw-test-wasm)

# 张量 IR 测试
add_executable(claw-test-tensorir
    src/tensorir/tensor_ir.cpp
    src/test/test_tensor_ir.cpp
)
add_test(NAME claw-test-tensorir COMMAND claw-test-tensorir)

# 自动调度器测试
add_executable(claw-test-auto-scheduler
    src/auto_scheduler/auto_scheduler.cpp
    src/auto_scheduler/vm_evaluator.cpp
    src/auto_scheduler/ml_evaluator.cpp
    src/auto_scheduler/schedule_cache.cpp
    src/auto_scheduler/schedule_space.cpp
    src/auto_scheduler/search_strategy.cpp
    src/test/test_auto_scheduler.cpp
)
add_test(NAME claw-test-auto-scheduler COMMAND claw-test-auto-scheduler)

# VM 评估器测试
add_executable(claw-test-vm-evaluator
    src/auto_scheduler/vm_evaluator.cpp
    src/auto_scheduler/schedule_space.cpp
    src/auto_scheduler/search_strategy.cpp
    src/auto_scheduler/schedule_cache.cpp
    src/auto_scheduler/auto_scheduler.cpp
    src/auto_scheduler/ml_evaluator.cpp
    src/tensorir/tensor_ir.cpp
    src/test/test_vm_evaluator.cpp
)
add_test(NAME claw-test-vm-evaluator COMMAND claw-test-vm-evaluator)

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

# 可执行文件: claw-lsp (Language Server Protocol)
add_executable(claw-lsp ${CLAW_LSP_SOURCES})

# 可执行文件: claw-repl (交互式解释器)
add_executable(claw-repl
    src/repl_main.cpp
    src/repl/claw_repl.cpp
    src/repl/claw_repl_integrated.cpp
    src/repl/repl.cpp
)

# 安装目标
install(TARGETS claw claw-opt claw-lsp claw-repl
    RUNTIME DESTINATION bin
)
