# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

Both CMake and Makefile are supported. The Makefile is the primary daily driver.

- `make claw` — Build the main compiler binary (`./claw`)
- `make claw-lsp` — Build the LSP server (`./claw-lsp`)
- `make claw-repl` — Build the REPL (`./claw-repl`)
- `make debug-claw` — Build a debug binary (`./claw-debug`) with `-g -O0 -DCLAW_DEBUG`
- `make clean` — Remove build artifacts
- `./build.sh` — CMake configure + build + `ctest` in `build/`

Requirements: `clang++` with C++17, LLVM (optional, for `claw-llvm` target).

## Running Tests

Individual test targets compile and run a single test binary:

- `make test-benchmark`
- `make test-cuda`
- `make test-package`
- `make test-debugger`
- `make test-auto-scheduler`
- `make test-vm-evaluator`
- `make test-tensorir`
- `make test-wasm`
- `make test-attribute`
- `make test-docgen`
- `make test-lexer`
- `make test` — Runs all of the above

CMake tests: `cd build && ctest --output-on-failure`

## Compiler Modes

The `./claw` binary supports multiple execution paths via CLI flags:

- `./claw --run file.claw` — AST interpreter
- `./claw -b file.claw` — Bytecode compilation + VM execution
- `./claw -j file.claw` — JIT compile and execute
- `./claw -H file.claw` — Hybrid: VM + JIT hot paths
- `./claw -C file.claw` — Generate C code to stdout
- `./claw -n file.claw` — Generate x86-64 native code
- `./claw -i` — Start REPL
- `./claw -t file.claw` — Print tokens
- `./claw -a file.claw` — Print AST

## High-Level Architecture

The compiler has multiple coexisting execution backends rather than a single codegen path.

**Frontend (shared)**
- `src/lexer/lexer.h` + `src/lexer/token.h` — Lexer (header-only implementation)
- `src/parser/parser.h` — Recursive-descent parser (header-only)
- `src/ast/ast.h` — AST node definitions
- `src/semantic/semantic_analyzer.cpp` — Semantic analysis + symbol tables
- `src/type/type_system.h` + `src/type/type_checker.cpp` — Type system with unification

**Intermediate layers**
- `src/ir/ir.h` + `src/ir/ir_generator.cpp` — SSA-based IR
- `src/bytecode/bytecode.h` + `src/bytecode/bytecode_compiler.cpp` — Bytecode instruction set and AST-to-bytecode compiler
- `src/pipeline/execution_pipeline.h` — Orchestrates Lexer → Parser → Bytecode → VM/JIT/C codegen in a single `CompilationResult`

**Backends**
- `src/interpreter/interpreter.h` — Tree-walk interpreter
- `src/vm/claw_vm.cpp` — Stack-based ClawVM that executes bytecode
- `src/jit/jit_compiler.cpp` + `src/jit/jit_runtime.cpp` — Method JIT + tracing JIT with deoptimization
- `src/codegen/c_codegen.h` + `src/codegen/native_codegen.cpp` — C code and native x86-64 codegen
- `src/emitter/{x86_64,arm64,riscv}_emitter.cpp` — Machine-code emitters
- `src/backend/cuda_codegen.cpp` — CUDA backend

**Tensor / ML subsystems**
- `src/tensorir/tensor_ir.cpp` — TensorIR for high-performance kernels
- `src/auto_scheduler/` — Auto-scheduler with search strategies and ML cost models
- `src/ml/` — Feature extraction and cost model adapter

**Tooling**
- `src/repl/` — Interactive REPL
- `src/debugger/` — Debugger + CLI
- `src/lsp/` — LSP server
- `src/package/` — Package manager with manifest parsing and dependency resolution

## Memory Model (for Codegen Work)

Claw uses deterministic scope-bound memory management: `let` bindings own their values, and the compiler inserts frees at block exit. Returns transfer ownership to the caller. There is no GC. When modifying codegen (C, native, or JIT), ensure `claw_alloc` / `claw_free` calls match the ownership rules documented in `docs/claw-memory-model.md`.

## Project Workflow

This repo uses the `brain` CLI for durable project memory. Before substantial work:

1. Run `brain prep --task "<task>"` (or `brain prep` if a session is active).
2. Read `AGENTS.md` and linked context files as needed.
3. Use `brain find claw-compiler` or `brain search "claw-compiler <topic>"` to retrieve prior context.
4. Use `brain session run -- <command>` for verification steps.
5. Finish with `brain session finish`.

Key design docs live in `docs/`:
- `docs/claw-language-spec.md` — Language specification
- `docs/claw-memory-model.md` — Ownership rules
- `docs/claw-type-system.md` — Type system details
- `docs/claw-compiler-design.md` — Compiler design
