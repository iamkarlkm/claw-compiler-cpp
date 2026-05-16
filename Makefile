# Claw Compiler - Comprehensive Makefile
# Supports: macOS, Linux
# No CMake required

CXX = clang++
LLVM_PREFIX := $(shell /usr/local/opt/llvm/bin/llvm-config --prefix 2>/dev/null || echo /usr/local/opt/llvm)
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-private-field -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-lambda-capture -Wno-missing-field-initializers -Wno-mismatched-tags -I. -Isrc -I$(LLVM_PREFIX)/include -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
DEBUG_FLAGS = -std=c++17 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-private-field -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-lambda-capture -Wno-missing-field-initializers -Wno-mismatched-tags -I. -Isrc -I$(LLVM_PREFIX)/include -DCLAW_DEBUG -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
LDFLAGS = -lpthread -lreadline -L/usr/local/Cellar/llvm/19.1.4/lib -lLLVM-19

# Link-time optimization (optional)
LTO ?= 0
ifeq ($(LTO),1)
    CXXFLAGS += -flto
    LDFLAGS += -flto
endif

# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS specific flags
    CXXFLAGS += -stdlib=libc++
endif

# Build directories
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug

# Auto-detect parallelism if not specified by user
NPROCS := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
    MAKEFLAGS += -j$(NPROCS)
endif

# ============================================================================
# Core Sources
# ============================================================================

CORE_SOURCES = \
    src/common/common.h \
    src/common/parse_cache.cpp \
    src/common/compilation_cache.cpp \
    src/lexer/lexer.h \
    src/lexer/token.h \
    src/parser/parser.h \
    src/ast/ast.h \
    src/ast/ast.cpp \
    src/ast/ast_compact_repr.cpp \
    src/type/type_system.h \
    src/type/type_checker.cpp \
    src/type/pattern_checker.cpp \
    src/type/type_inference.cpp \
    src/semantic/semantic_analyzer.cpp \
    src/optimizer/tree_shaker.cpp \
    src/optimizer/constant_folder.cpp \
    src/optimizer/control_flow_simplifier.cpp \
    src/optimizer/dead_code_eliminator.cpp \
    src/optimizer/peephole_optimizer.cpp \
    src/optimizer/function_inliner.cpp \
    src/optimizer/tail_call_optimizer.cpp \
    src/optimizer/algebraic_simplifier.cpp \
    src/optimizer/monomorphizer.cpp \
    src/optimizer/iterator_desugarer.cpp \
    src/ast/clone.cpp \
    src/ir/ir.cpp \
    src/ir/ir_generator.cpp \
    src/ir/ir_enhanced.cpp \
    src/ir/ir_generator_enhanced.cpp \
    src/ir/ir_optimizer.cpp \
    src/ir_bytecode_bridge.cpp \
    src/bridge/ir_bytecode_bridge.cpp \
    src/bytecode/bytecode.cpp \
    src/bytecode/bytecode_compiler.cpp \
    src/bytecode/bytecode_compiler_simple.cpp \
    src/bytecode/bytecode_executor.cpp \
    src/vm/claw_vm.cpp \
    src/interpreter/interpreter.cpp \
    src/pipeline/execution_engine.cpp \
    src/pipeline/execution_engine_enhanced.cpp \
    src/pipeline/execution_pipeline.cpp \
    src/pipeline/perf_profiler.cpp \
    src/codegen/codegen.cpp \
    src/codegen/native_codegen.cpp \
    src/codegen/macho_writer.cpp \
    src/codegen/linker_integration.cpp \
    src/native_codegen/native_codegen.cpp \
    src/emitter/x86_64_emitter.cpp \
    src/emitter/arm64_emitter.cpp \
    src/emitter/riscv_emitter.cpp \
    src/codegen/x86_64_codegen.cpp \
    src/emitter/reg_alloc.cpp \
    src/jit/jit_compiler.cpp \
    src/jit/jit_runtime.cpp \
    src/jit/jit_stdlib_integration.cpp \
    src/jit/deoptimization.cpp \
    src/jit/stack_frame.cpp \
    src/jit/optimizations.cpp \
    src/jit/tracing_jit.cpp \
    src/jit/type_profiler.cpp \
    src/jit/inline_cache.cpp \
    src/jit/hot_spot.cpp \
    src/module/module.cpp \
    src/stdlib/stdlib.cpp \
    src/stdlib/stdlib_bytecode_integration.cpp \
    src/package/package_manager.cpp \
    src/package/manifest_parser.cpp \
    src/package/dependency_resolver.cpp \
    src/package/lock_file.cpp \
    src/auto_scheduler/auto_scheduler.cpp \
    src/auto_scheduler/vm_evaluator.cpp \
    src/auto_scheduler/ml_evaluator.cpp \
    src/auto_scheduler/schedule_cache.cpp \
    src/auto_scheduler/schedule_space.cpp \
    src/auto_scheduler/search_strategy.cpp \
    src/ml/feature_extractor.cpp \
    src/ml/ml_cost_model.cpp \
    src/ml/ml_cost_model_adapter.cpp \
    src/backend/cuda_codegen.cpp \
    src/benchmark/benchmark.cpp \
    src/tensorir/tensor_ir.cpp \
    src/tensorir/tensor_ir_generator.cpp \
    src/repl/claw_repl.cpp \
    src/repl/claw_repl_integrated.cpp \
    src/repl/repl.cpp \
    src/debugger/claw_debugger.cpp \
    src/debugger/claw_debugger_cli.cpp \
    src/json/json.h \
    src/main.cpp

CORE_CPP_SOURCES = $(filter %.cpp,$(CORE_SOURCES))
CORE_OBJECTS     = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CORE_CPP_SOURCES))
DEBUG_OBJECTS    = $(patsubst %.cpp,$(DEBUG_BUILD_DIR)/%.o,$(CORE_CPP_SOURCES))

# ============================================================================
# Test Sources
# ============================================================================

TEST_BENCHMARK_SOURCES = src/benchmark/benchmark.cpp src/test/test_benchmark.cpp
TEST_PACKAGE_SOURCES = src/package/package_manager.cpp src/package/manifest_parser.cpp \
    src/package/dependency_resolver.cpp src/package/lock_file.cpp src/test/test_package_manager.cpp
TEST_CUDA_SOURCES = src/backend/cuda_codegen.cpp src/tensorir/tensor_ir.cpp src/test/test_cuda_codegen.cpp
TEST_DEBUGGER_SOURCES = src/debugger/claw_debugger.cpp src/jit/deoptimization.cpp src/jit/stack_frame.cpp \
    src/test/test_deoptimization.cpp src/test/test_stack_frame.cpp
TEST_AUTO_SCHEDULER_SOURCES = src/auto_scheduler/auto_scheduler.cpp src/auto_scheduler/vm_evaluator.cpp src/auto_scheduler/ml_evaluator.cpp \
    src/auto_scheduler/schedule_cache.cpp src/auto_scheduler/schedule_space.cpp \
    src/auto_scheduler/search_strategy.cpp src/ml/ml_cost_model.cpp src/ml/ml_cost_model_adapter.cpp \
    src/test/test_ml_cost_model_adapter.cpp
TEST_VM_EVALUATOR_SOURCES = src/auto_scheduler/vm_evaluator.cpp src/auto_scheduler/schedule_space.cpp \
    src/auto_scheduler/search_strategy.cpp src/auto_scheduler/schedule_cache.cpp \
    src/auto_scheduler/auto_scheduler.cpp src/auto_scheduler/ml_evaluator.cpp \
    src/pipeline/perf_profiler.cpp src/tensorir/tensor_ir.cpp src/test/test_vm_evaluator.cpp \
    src/vm/claw_vm.cpp src/jit/jit_runtime.cpp src/ml/ml_cost_model.cpp src/ml/ml_cost_model_adapter.cpp \
    src/bytecode/bytecode.cpp src/bytecode/bytecode_compiler.cpp src/stdlib/stdlib.cpp
# Note: test-tensorir removed - no test file exists
TEST_WASM_SOURCES = src/emitter/wasm/wasm_backend.cpp src/emitter/wasm/wasm_ir_generator.cpp src/test/test_wasm_ir.cpp
TEST_ATTRIBUTE_SOURCES = src/frontend/attribute.cpp src/test/test_attribute.cpp
TEST_DOCGEN_SOURCES = src/tools/doc_generator.cpp src/test/test_doc_generator.cpp
TEST_IR_PASSES_SOURCES = src/ir/ir.cpp src/ir/ir_enhanced.cpp src/ir/ir_optimizer.cpp src/benchmark/benchmark.cpp test/benchmark_ir_passes.cpp
TEST_TREE_SHAKER_SOURCES = src/optimizer/tree_shaker.cpp src/test/test_tree_shaker.cpp
TEST_CONSTANT_FOLDER_SOURCES = src/optimizer/constant_folder.cpp src/test/test_constant_folder.cpp
TEST_CONTROL_FLOW_SIMPLIFIER_SOURCES = src/optimizer/control_flow_simplifier.cpp src/test/test_control_flow_simplifier.cpp
TEST_DEAD_CODE_ELIMINATOR_SOURCES = src/optimizer/dead_code_eliminator.cpp src/test/test_dead_code_eliminator.cpp
TEST_PEEPHOLE_OPTIMIZER_SOURCES = src/optimizer/peephole_optimizer.cpp src/test/test_peephole_optimizer.cpp
TEST_FUNCTION_INLINER_SOURCES = src/optimizer/function_inliner.cpp src/ast/clone.cpp src/test/test_function_inliner.cpp
TEST_TAIL_CALL_OPTIMIZER_SOURCES = src/optimizer/tail_call_optimizer.cpp src/ast/clone.cpp src/test/test_tail_call_optimizer.cpp
TEST_ALGEBRAIC_SIMPLIFIER_SOURCES = src/optimizer/algebraic_simplifier.cpp src/ast/clone.cpp src/test/test_algebraic_simplifier.cpp
TEST_PATTERN_CHECKER_SOURCES = src/type/type_checker.cpp src/type/pattern_checker.cpp src/test/test_pattern_checker.cpp
TEST_MONOMORPHIZER_SOURCES = src/optimizer/monomorphizer.cpp src/ast/clone.cpp src/ast/ast.cpp src/test/test_monomorphizer.cpp
TEST_ITERATOR_DESUGARER_SOURCES = src/optimizer/iterator_desugarer.cpp src/ast/clone.cpp src/ast/ast.cpp src/test/test_iterator_desugarer.cpp
TEST_TYPE_INFERENCE_SOURCES = src/type/type_inference.cpp src/type/type_checker.cpp src/type/pattern_checker.cpp src/ast/ast.cpp src/ast/clone.cpp src/test/test_type_inference.cpp
TEST_COMPACT_AST_SOURCES = src/ast/ast_compact_repr.cpp src/ast/ast.cpp src/ast/clone.cpp src/test/test_compact_ast.cpp

# ============================================================================
# Targets
# ============================================================================

.PHONY: all clean test help \
    test-benchmark test-cuda test-package test-debugger \
    test-auto-scheduler test-wasm test-attribute test-docgen test-vm-evaluator \
    test-ir-passes test-lexer test-aot test-tree-shaker test-constant-folder test-control-flow-simplifier test-dead-code-eliminator test-bytecode-opt test-peephole-optimizer test-function-inliner test-tail-call-optimizer test-algebraic-simplifier test-pattern-checker test-monomorphizer test-iterator-desugarer test-type-inference test-compact-ast

all: claw claw-lsp claw-repl

help:
	@echo "Claw Compiler Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build all main targets"
	@echo "  claw             - Build main compiler"
	@echo "  claw-lsp         - Build LSP server"
	@echo "  claw-repl        - Build REPL"
	@echo "  test             - Run all tests"
	@echo "  test-benchmark   - Run benchmark framework tests"
	@echo "  test-cuda        - Run CUDA codegen tests"
	@echo "  test-package     - Run package manager tests"
	@echo "  test-attribute   - Run attribute/macro system tests"
	@echo "  clean            - Clean build artifacts"
	@echo ""
	@echo "Build options:"
	@echo "  LTO=1            - Enable link-time optimization"
	@echo ""

# Main compiler
claw: $(CORE_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# LSP Server
claw-lsp: src/lsp/lsp_main.cpp src/lsp/lsp_protocol.cpp src/lsp/lsp_server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# REPL
claw-repl: src/repl_main.cpp src/repl/claw_repl.cpp src/repl/claw_repl_integrated.cpp src/repl/repl.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# ============================================================================
# Tests
# ============================================================================

test: test-benchmark test-cuda test-package test-attribute test-docgen test-ir-passes test-lexer test-tree-shaker test-constant-folder test-control-flow-simplifier test-dead-code-eliminator test-bytecode-opt test-peephole-optimizer test-function-inliner test-tail-call-optimizer test-algebraic-simplifier test-pattern-checker test-monomorphizer test-iterator-desugarer test-type-inference test-compact-ast test-aot
	@echo ""
	@echo "=== All Tests Completed ==="

test-benchmark:
	$(CXX) $(CXXFLAGS) -o test_benchmark $(TEST_BENCHMARK_SOURCES)
	@./test_benchmark

test-cuda:
	$(CXX) $(CXXFLAGS) -o test_cuda $(TEST_CUDA_SOURCES)
	@./test_cuda

test-package:
	$(CXX) $(CXXFLAGS) -o test_package $(TEST_PACKAGE_SOURCES)
	@./test_package

test-debugger:
	$(CXX) $(CXXFLAGS) -o test_debugger $(TEST_DEBUGGER_SOURCES)
	@./test_debugger

test-auto-scheduler:
	$(CXX) $(CXXFLAGS) -o test_auto_scheduler $(TEST_AUTO_SCHEDULER_SOURCES)
	@./test_auto_scheduler

test-vm-evaluator:
	$(CXX) $(CXXFLAGS) -o test_vm_evaluator $(TEST_VM_EVALUATOR_SOURCES)
	@./test_vm_evaluator

test-wasm:
	$(CXX) $(CXXFLAGS) -o test_wasm $(TEST_WASM_SOURCES)
	@./test_wasm

test-attribute:
	$(CXX) $(CXXFLAGS) -o test_attribute $(TEST_ATTRIBUTE_SOURCES)
	@./test_attribute

test-docgen:
	$(CXX) $(CXXFLAGS) -o test_docgen $(TEST_DOCGEN_SOURCES)
	@./test_docgen

test-ir-passes:
	$(CXX) $(CXXFLAGS) -o test_ir_passes $(TEST_IR_PASSES_SOURCES)
	@./test_ir_passes

test-tree-shaker:
	$(CXX) $(CXXFLAGS) -o test_tree_shaker $(TEST_TREE_SHAKER_SOURCES)
	@./test_tree_shaker

test-constant-folder:
	$(CXX) $(CXXFLAGS) -o test_constant_folder $(TEST_CONSTANT_FOLDER_SOURCES)
	@./test_constant_folder

test-control-flow-simplifier:
	$(CXX) $(CXXFLAGS) -o test_control_flow_simplifier $(TEST_CONTROL_FLOW_SIMPLIFIER_SOURCES)
	@./test_control_flow_simplifier

test-dead-code-eliminator:
	$(CXX) $(CXXFLAGS) -o test_dead_code_eliminator $(TEST_DEAD_CODE_ELIMINATOR_SOURCES)
	@./test_dead_code_eliminator

test-peephole-optimizer:
	$(CXX) $(CXXFLAGS) -o test_peephole_optimizer $(TEST_PEEPHOLE_OPTIMIZER_SOURCES)
	@./test_peephole_optimizer

test-function-inliner:
	$(CXX) $(CXXFLAGS) -o test_function_inliner $(TEST_FUNCTION_INLINER_SOURCES)
	@./test_function_inliner

test-tail-call-optimizer:
	$(CXX) $(CXXFLAGS) -o test_tail_call_optimizer $(TEST_TAIL_CALL_OPTIMIZER_SOURCES)
	@./test_tail_call_optimizer

test-algebraic-simplifier:
	$(CXX) $(CXXFLAGS) -o test_algebraic_simplifier $(TEST_ALGEBRAIC_SIMPLIFIER_SOURCES)
	@./test_algebraic_simplifier

test-pattern-checker:
	$(CXX) $(CXXFLAGS) -o test_pattern_checker $(TEST_PATTERN_CHECKER_SOURCES)
	@./test_pattern_checker

test-monomorphizer:
	$(CXX) $(CXXFLAGS) -o test_monomorphizer $(TEST_MONOMORPHIZER_SOURCES)
	@./test_monomorphizer

test-iterator-desugarer:
	$(CXX) $(CXXFLAGS) -o test_iterator_desugarer $(TEST_ITERATOR_DESUGARER_SOURCES)
	@./test_iterator_desugarer

test-type-inference:
	$(CXX) $(CXXFLAGS) -o test_type_inference $(TEST_TYPE_INFERENCE_SOURCES)
	@./test_type_inference

test-compact-ast:
	$(CXX) $(CXXFLAGS) -o test_compact_ast $(TEST_COMPACT_AST_SOURCES)
	@./test_compact_ast

test-aot: claw
	@echo "=== AOT Native Codegen Tests ==="
	@echo "fn main() { print(10 + 20); }" > /tmp/_aot_simple.claw
	@./claw --aot -o /tmp/_aot_simple /tmp/_aot_simple.claw 2>/dev/null
	@test "`/tmp/_aot_simple`" = "30" || (echo "AOT simple arithmetic failed"; exit 1)
	@echo "fn main() { let a = 5; if a > 3 { print(100); } else { print(200); } }" > /tmp/_aot_if.claw
	@./claw --aot -o /tmp/_aot_if /tmp/_aot_if.claw 2>/dev/null
	@test "`/tmp/_aot_if`" = "100" || (echo "AOT if-true failed"; exit 1)
	@echo "fn main() { let a = 1; if a > 3 { print(100); } else { print(200); } }" > /tmp/_aot_if2.claw
	@./claw --aot -o /tmp/_aot_if2 /tmp/_aot_if2.claw 2>/dev/null
	@test "`/tmp/_aot_if2`" = "200" || (echo "AOT if-false failed"; exit 1)
	@echo "fn main() { let i = 0; while i < 5 { i = i + 1; } print(i); }" > /tmp/_aot_loop.claw
	@./claw --aot -o /tmp/_aot_loop /tmp/_aot_loop.claw 2>/dev/null
	@test "`/tmp/_aot_loop`" = "5" || (echo "AOT loop failed"; exit 1)
	@echo "fn add(a,b){ return a+b; } fn main(){ print(add(3,4)); }" > /tmp/_aot_func.claw
	@./claw --aot -o /tmp/_aot_func /tmp/_aot_func.claw 2>/dev/null
	@test "`/tmp/_aot_func`" = "7" || (echo "AOT function call failed"; exit 1)
	@echo "fn factorial(n){ if n<=1 { return 1; } return n*factorial(n-1); } fn main(){ print(factorial(5)); }" > /tmp/_aot_rec.claw
	@./claw --aot -o /tmp/_aot_rec /tmp/_aot_rec.claw 2>/dev/null
	@test "`/tmp/_aot_rec`" = "120" || (echo "AOT recursion failed"; exit 1)
	@echo "fn main() { let a = 5; if a > 3 { if a < 10 { print(1); } else { print(2); } } else { print(3); } }" > /tmp/_aot_nested.claw
	@./claw --aot -o /tmp/_aot_nested /tmp/_aot_nested.claw 2>/dev/null
	@test "`/tmp/_aot_nested`" = "1" || (echo "AOT nested if failed"; exit 1)
	@echo "fn main() { let a = 5; if a == 5 { print(10); } else { print(20); } }" > /tmp/_aot_eq.claw
	@./claw --aot -o /tmp/_aot_eq /tmp/_aot_eq.claw 2>/dev/null
	@test "`/tmp/_aot_eq`" = "10" || (echo "AOT equality failed"; exit 1)
	@echo "fn main() { let a = 5; if a != 5 { print(10); } else { print(20); } }" > /tmp/_aot_ne.claw
	@./claw --aot -o /tmp/_aot_ne /tmp/_aot_ne.claw 2>/dev/null
	@test "`/tmp/_aot_ne`" = "20" || (echo "AOT not-equal failed"; exit 1)
	@echo "fn main() { let a = 5; if a <= 5 { print(10); } else { print(20); } }" > /tmp/_aot_le.claw
	@./claw --aot -o /tmp/_aot_le /tmp/_aot_le.claw 2>/dev/null
	@test "`/tmp/_aot_le`" = "10" || (echo "AOT less-equal failed"; exit 1)
	@echo "fn main() { let a = 5; if a >= 6 { print(10); } else { print(20); } }" > /tmp/_aot_ge.claw
	@./claw --aot -o /tmp/_aot_ge /tmp/_aot_ge.claw 2>/dev/null
	@test "`/tmp/_aot_ge`" = "20" || (echo "AOT greater-equal failed"; exit 1)
	@echo "fn foo() { return 42; } fn main() { print(foo()); }" > /tmp/_aot_zeroarg.claw
	@./claw --aot -o /tmp/_aot_zeroarg /tmp/_aot_zeroarg.claw 2>/dev/null
	@test "`/tmp/_aot_zeroarg`" = "42" || (echo "AOT zero-arg function failed"; exit 1)
	@echo "fn add3(a,b,c){ return a+b+c; } fn main(){ print(add3(1,2,3)); }" > /tmp/_aot_3arg.claw
	@./claw --aot -o /tmp/_aot_3arg /tmp/_aot_3arg.claw 2>/dev/null
	@test "`/tmp/_aot_3arg`" = "6" || (echo "AOT 3-arg function failed"; exit 1)
	@echo "fn main() { let a = -5; let b = 3; print(a + b); }" > /tmp/_aot_neg.claw
	@./claw --aot -o /tmp/_aot_neg /tmp/_aot_neg.claw 2>/dev/null
	@test "`/tmp/_aot_neg`" = "-2" || (echo "AOT negative literal failed"; exit 1)
	@echo "fn main() { let a = 1; let b = 0; if a && b { print(10); } else { print(20); } }" > /tmp/_aot_and.claw
	@./claw --aot -o /tmp/_aot_and /tmp/_aot_and.claw 2>/dev/null
	@test "`/tmp/_aot_and`" = "20" || (echo "AOT logical AND failed"; exit 1)
	@echo "fn main() { let a = 1; let b = 0; if a || b { print(10); } else { print(20); } }" > /tmp/_aot_or.claw
	@./claw --aot -o /tmp/_aot_or /tmp/_aot_or.claw 2>/dev/null
	@test "`/tmp/_aot_or`" = "10" || (echo "AOT logical OR failed"; exit 1)
	@echo "fn main() { let a = 0; if !a { print(10); } else { print(20); } }" > /tmp/_aot_not.claw
	@./claw --aot -o /tmp/_aot_not /tmp/_aot_not.claw 2>/dev/null
	@test "`/tmp/_aot_not`" = "10" || (echo "AOT logical NOT failed"; exit 1)
	@rm -f /tmp/_aot_*.claw /tmp/_aot_simple /tmp/_aot_simple.o /tmp/_aot_if /tmp/_aot_if.o /tmp/_aot_if2 /tmp/_aot_if2.o /tmp/_aot_loop /tmp/_aot_loop.o /tmp/_aot_func /tmp/_aot_func.o /tmp/_aot_rec /tmp/_aot_rec.o /tmp/_aot_nested /tmp/_aot_nested.o /tmp/_aot_eq /tmp/_aot_eq.o /tmp/_aot_ne /tmp/_aot_ne.o /tmp/_aot_le /tmp/_aot_le.o /tmp/_aot_ge /tmp/_aot_ge.o /tmp/_aot_zeroarg /tmp/_aot_zeroarg.o /tmp/_aot_3arg /tmp/_aot_3arg.o /tmp/_aot_neg /tmp/_aot_neg.o /tmp/_aot_and /tmp/_aot_and.o /tmp/_aot_or /tmp/_aot_or.o /tmp/_aot_not /tmp/_aot_not.o
	@echo "AOT tests passed"

test-bytecode-opt: claw
	@echo "=== Bytecode VM Optimization Tests (-O1) ==="
	@rm -rf ~/.claw/cache/compile/*
	@echo "fn main() { print(30); }" > /tmp/_bc_lit.claw
	@test "`./claw -b -O1 /tmp/_bc_lit.claw 2>/dev/null | tail -2 | head -1`" = "30" || (echo "BC literal print failed"; exit 1)
	@echo "fn main() { print(10 + 20); }" > /tmp/_bc_fold.claw
	@test "`./claw -b -O1 /tmp/_bc_fold.claw 2>/dev/null | tail -2 | head -1`" = "30" || (echo "BC constant folding failed"; exit 1)
	@echo "fn main() { if true { print(100); } else { print(200); } }" > /tmp/_bc_cf_true.claw
	@test "`./claw -b -O1 /tmp/_bc_cf_true.claw 2>/dev/null | tail -2 | head -1`" = "100" || (echo "BC if-true simplification failed"; exit 1)
	@echo "fn main() { if false { print(100); } else { print(200); } }" > /tmp/_bc_cf_false.claw
	@test "`./claw -b -O1 /tmp/_bc_cf_false.claw 2>/dev/null | tail -2 | head -1`" = "200" || (echo "BC if-false simplification failed"; exit 1)
	@echo "fn main() { while false { print(999); } print(42); }" > /tmp/_bc_while_false.claw
	@test "`./claw -b -O1 /tmp/_bc_while_false.claw 2>/dev/null | tail -2 | head -1`" = "42" || (echo "BC while-false removal failed"; exit 1)
	@echo "fn foo() { print(10); return; print(20); } fn main() { foo(); }" > /tmp/_bc_dce.claw
	@test "`./claw -b -O1 /tmp/_bc_dce.claw 2>/dev/null | tail -2 | head -1`" = "10" || (echo "BC dead code elimination failed"; exit 1)
	@echo "fn main() { let a = 5; if a > 3 { print(100); } else { print(200); } }" > /tmp/_bc_runtime_if.claw
	@test "`./claw -b -O1 /tmp/_bc_runtime_if.claw 2>/dev/null | tail -2 | head -1`" = "100" || (echo "BC runtime if failed"; exit 1)
	@echo "fn main() { print(2 * 3 + 4); }" > /tmp/_bc_nested_fold.claw
	@test "`./claw -b -O1 /tmp/_bc_nested_fold.claw 2>/dev/null | tail -2 | head -1`" = "10" || (echo "BC nested constant folding failed"; exit 1)
	@echo "fn main() { let a = 1; let b = 0; if a && b { print(10); } else { print(20); } }" > /tmp/_bc_and.claw
	@test "`./claw -b -O1 /tmp/_bc_and.claw 2>/dev/null | tail -2 | head -1`" = "20" || (echo "BC logical AND failed"; exit 1)
	@echo "fn main() { let a = 1; let b = 0; if a || b { print(10); } else { print(20); } }" > /tmp/_bc_or.claw
	@test "`./claw -b -O1 /tmp/_bc_or.claw 2>/dev/null | tail -2 | head -1`" = "10" || (echo "BC logical OR failed"; exit 1)
	@echo "fn add(a,b){ return a+b; } fn main(){ print(add(3,4)); }" > /tmp/_bc_inline.claw
	@test "`./claw -b -O1 /tmp/_bc_inline.claw 2>/dev/null | tail -2 | head -1`" = "7" || (echo "BC function inlining failed"; exit 1)
	@echo "fn double(x){ return x*2; } fn main(){ print(double(5)); }" > /tmp/_bc_inline2.claw
	@test "`./claw -b -O1 /tmp/_bc_inline2.claw 2>/dev/null | tail -2 | head -1`" = "10" || (echo "BC function inlining 2 failed"; exit 1)
	@echo "fn count(n,acc){ if n<=0 { return acc; } return count(n-1,acc+1); } fn main(){ print(count(1000,0)); }" > /tmp/_bc_tco.claw
	@test "`./claw -b -O1 /tmp/_bc_tco.claw 2>/dev/null | tail -2 | head -1`" = "1000" || (echo "BC tail call optimization failed"; exit 1)
	@echo "fn main() { let sum = 0; for x in [1, 2, 3, 4, 5] { sum = sum + x; } print(sum); }" > /tmp/_bc_for.claw
	@test "`./claw -b -O1 /tmp/_bc_for.claw 2>/dev/null | tail -2 | head -1`" = "15" || (echo "BC for-loop desugaring failed"; exit 1)
	@echo "fn main() { let sum = 0; for i in 0..5 { sum = sum + i; } print(sum); }" > /tmp/_bc_range.claw
	@test "`./claw -b -O1 /tmp/_bc_range.claw 2>/dev/null | tail -2 | head -1`" = "10" || (echo "BC range iteration failed"; exit 1)
	@rm -f /tmp/_bc_*.claw
	@echo "Bytecode optimization tests passed"

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -f claw claw-lsp claw-repl claw-debug
	rm -f test_benchmark test_cuda test_package test_debugger
	rm -f test_auto_scheduler test_tensorir test_wasm test_attribute test_docgen test_vm_evaluator test_ir_passes
	rm -rf $(BUILD_DIR)

# ============================================================================
# Debug builds
# ============================================================================

debug-claw: $(DEBUG_OBJECTS)
	$(CXX) $(DEBUG_FLAGS) -o claw-debug $^ $(LDFLAGS)

$(DEBUG_BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEBUG_FLAGS) -c -o $@ $<

# ============================================================================
# Installation
# ============================================================================

install: all
	@echo "Installing Claw Compiler..."
	@mkdir -p $(DESTDIR)/usr/local/bin
	@cp claw claw-lsp claw-repl $(DESTDIR)/usr/local/bin/
	@echo "Installation complete!"

# ============================================================================
# 测试目标
# ============================================================================
TEST_CXXFLAGS = -std=c++17 -I. -Isrc

tests/test_lexer: tests/test_lexer.cpp src/lexer/lexer.h src/lexer/token.h tests/claw_test.h
	$(CXX) $(TEST_CXXFLAGS) -o $@ tests/test_lexer.cpp

test-lexer: tests/test_lexer
	@./tests/test_lexer

.PHONY: test-lexer
