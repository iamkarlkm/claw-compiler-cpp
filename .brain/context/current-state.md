---
title: Current State - Production Readiness Review 2026-06-05
updated: "2026-06-09T10:00:00Z"
---
## Repository

- Project: `claw-compiler`
- Root: `.`
- Runtime: `unknown`
- Current branch: `main`
- Version: `0.2.0`
- Source: ~319 files, ~90K LoC C++, ~5K LoC tests

## Build Health

- **Makefile**: healthy, dependency auto-detection works (clang++, LLVM, readline, libmsquic)
- **Compiler warnings**: 0 warnings on clean build (down from 182 in April)
- **Binaries built**: `claw` (5.9M), `claw-lsp` (6.0M), `claw-repl` (5.8M), `claw-debugger` (5.8M)
- **CI/CD**: GitHub Actions with macOS + Linux + Windows build + test + benchmark; release workflow with checksums

## Test Summary (all passing)

| Suite | Count | Status |
|-------|-------|--------|
| Lexer | 29/29 | pass |
| Benchmark framework | 20/20 | pass |
| CUDA codegen | 17/17 | pass |
| Package manager | 24/24 | pass |
| Attribute/macro | 17/17 | pass |
| Doc generator | 17/17 | pass |
| IR passes benchmark | pass | pass |
| Tree shaker | 6/6 | pass |
| Constant folder | 7/7 | pass |
| Constant propagator | 8/8 | pass |
| Control flow simplifier | 8/8 | pass |
| Dead code eliminator | 5/5 | pass |
| Peephole optimizer | 7/7 | pass |
| Function inliner | 7/7 | pass |
| Tail call optimizer | 5/5 | pass |
| Algebraic simplifier | 17/17 | pass |
| Pattern checker | 12/12 | pass |
| Type inference | pass | pass |
| Monomorphizer | 4/4 | pass |
| Iterator desugarer | pass | pass |
| Compact AST | pass | pass |
| Implicit generic | 16/16 | pass |
| Tensor IR | pass | pass |
| Bytecode optimization | pass | pass |
| AOT end-to-end | 9 binaries | pass |

## Known Issues

1. ~~Bytecode VM - if-expression implicit return~~: Verified working across all test cases; function-end POP->RET replacement handles both branches correctly.
2. ~~Missing test binaries~~: `test_auto_scheduler`, `test_debugger`, `test_wasm` are compiled by default and passing
3. ~~AOT linker warning~~: Fixed by adding `-mmacosx-version-min=10.15` to runtime stub compilation in `linker_integration.cpp`
4. ~~Bytecode VM `return;` bug~~: Fixed `compileReturnStmt` to emit `RET_NULL` instead of buggy double `PUSH`; implicit return logic now checks for `RET_NULL`
5. ~~Struct constructors in expressions~~: Verified working in `let` initializer, function arguments, return values, and array literals.
6. ~~Integration test consistency~~: Fixed `--mode=bytecode` parsing, VM string quoting, and added missing AST array/string builtins
7. ~~Bytecode VM output consistency~~: Fixed by clean rebuild; stale object files caused `bytecode::Module` corruption in `main.o`. All integration tests now pass.
8. ~~Bytecode VM loop body premature return~~: Fixed by saving/restoring `isTailContext_ = false` during loop body compilation in `bytecode_compiler.cpp`
9. ~~AST interpreter implicit return~~: Added `execute_body` helper for functions, impl methods, and lambdas; captures last expression value without double-evaluation
10. ~~AST interpreter missing array/string builtins~~: Added `arr_len`, `arr_push`, `arr_range`, `str_len`, `str_upper`, `str_contains` to `Runtime` builtins
11. ~~Makefile `test-integration` dependency~~: Added `claw` as a prerequisite so `make test` works after `make clean`
12. ~~Clean build warnings~~: Added `-Wno-missing-braces` to suppress external header warning; build now produces zero warnings
13. ~~VERSION file C++ header shadowing~~: Removed `VERSION` file and `!VERSION` gitignore rule; on case-insensitive macOS it shadowed the C++20 `<version>` standard library header, breaking compilation
14. ~~LSP symbol extraction in function bodies~~: `extractSymbolsFromStatement` now recurses into `FunctionStmt` bodies, enabling hover, goto-definition, and documentSymbol for local variables and nested declarations
15. ~~AOT jump target out of range~~: Fixed by (a) bytecode compiler omitting dead JMPs after branches ending in RET/RET_NULL in `compileIfStmt`, and (b) native codegen recording end-of-function label position for jumps targeting past the last instruction

## Execution Mode Verification

| Mode | Command | Status | Notes |
|------|---------|--------|-------|
| AST Interpreter | `--run` | works | basic scripts execute correctly; self receiver supported; implicit returns for functions/impls/lambdas; array/string builtins added |
| C CodeGen | `-C` | works | generates C code |
| AOT Native | `--aot` | works | produces runnable Mach-O binary |
| Bytecode VM | `--mode=bytecode` | works | compiles and executes correctly; integration tests pass |
| JIT | `--mode=jit` | works | JIT compilation and execution verified; basic tests pass; void builtin return values cleaned up |

## Recently Completed

- **Self receiver syntax**: `obj.method()` parses and executes correctly; `self` is injected as first argument for impl methods
- **Implicit returns**: function and impl method bodies now capture the last expression value as implicit return (matching lambda behavior)
- **Release automation**: release.yml workflow builds, packages, and publishes macOS/Linux tar.gz + Docker image on tag push
- **claw-debugger**: added to `make all`; core execution commands (run/step/continue/print) functional; fixed segfault on exit
- **Coverage**: `make coverage` works with lcov 2.x; coverage job added to CI
- **Windows CI**: new ci-windows.yml for MSYS2/MinGW builds
- **Release checksums + GPG signing**: SHA256SUMS and `.asc` signatures generated and attached to GitHub Releases
- **Bytecode VM implicit return**: simple function implicit returns now work in VM mode
- **AOT linker warning fixed**: `compile_runtime_stub()` now passes `-mmacosx-version-min=10.15` on macOS
- **Bytecode `return;` fixed**: `compileReturnStmt` now emits `RET_NULL` instead of buggy double `PUSH`; implicit return check handles `RET_NULL`
- **LSP/REPL smoke-tested**: `claw-lsp` responds to initialize; `claw-repl` executes expressions and variables
- **Integration test consistency fixed**: `--mode=bytecode` now works with `=` syntax; VM string quoting matches AST; missing AST builtins added
- **Debugger source-level breakpoints**: Bytecode compiler emits `source_file` and `line_numbers`; debugger resolves actual source locations for breakpoint hits
- **REPL spurious error fixed**: Added `repl_mode` flag to interpreter; `claw-repl` no longer prints "No main function found" for top-level statements
- **LSP/REPL version bumped**: Version strings updated from 0.1.0 to 0.2.0 across LSP server and REPL binaries
- **BytecodeCompiler crash fixed**: Execution pipeline now uses real `BytecodeCompiler` instead of stub; `compileFunction` ctx save/restore no longer crashes; `debugInfo_` defaults to `false` to avoid debug info corruption
- **AOT jump target out of range fixed**: `compileIfStmt` no longer emits dead JMPs after branches ending in RET/RET_NULL; native codegen records end-of-function label position for defensive jump resolution; `make test-aot` passes all 17 test cases

## Production Deployment Readiness

| Dimension | Score | Notes |
|-----------|-------|-------|
| Language core | 85% | rich feature set, generics, pattern matching, effects, self receiver |
| Compiler backends | 80% | C/LLVM/AOT/Bytecode/JIT/WASM emitters exist |
| Optimizer pipeline | 90% | 13+ passes, -O0/-O1/-O2/-O3, iterative convergence |
| Execution engines | 80% | interpreter solid; bytecode and JIT verified on core cases |
| Developer tools | 80% | LSP supports initialize, hover, goto-definition, documentSymbol, completion, references, rename, semantic tokens; REPL executes expressions cleanly; debugger source-level breakpoints functional |
| Package manager | 80% | full manifest/resolve/lock/cache implementation |
| Test coverage | 80% | many unit tests, coverage job in CI, integration tests pass across AST/Bytecode/JIT/AOT |
| Build system | 85% | robust Makefile, auto-detection, install target |
| CI/CD | 80% | build+test+benchmark+coverage on macOS/Linux/Windows; release workflow with checksums+GPG+Windows+deb |
| Documentation | 60% | extensive design docs, getting-started guides, API reference started |
| Code quality | 95% | zero compiler warnings on clean build |
| Release packaging | 90% | tar.gz/zip + Docker + Homebrew + deb/rpm scripts + checksums + GPG signing + Windows artifact |
| **Overall** | **~84%** | core compiler strong; execution stability verified; release tooling largely complete; zero-warning clean build; LSP feature-complete for core operations |

## Deployment Gaps (Remaining)

1. ~~Integration test consistency~~: Fixed `--mode=bytecode` parsing, VM string quoting, and added missing AST builtins
2. ~~Debugger source-level breakpoints~~: Bytecode compiler emits `source_file` and `line_numbers`; debugger `current_vm_location()` resolves actual source locations; execution loop checks breakpoints against resolved location
3. ~~No deb/rpm packages~~: `scripts/build-deb.sh` and `scripts/build-rpm.sh` added; release workflow includes `.deb` build job
4. ~~No Windows installer~~: Windows zip packaging added to release workflow; MSI installer could be a future enhancement
5. ~~CHANGELOG automation~~: `scripts/generate-changelog.sh` generates changelog entries from git commits
