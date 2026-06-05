---
title: Current State - Production Readiness Review 2026-06-05
updated: "2026-06-05T11:27:13Z"
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
- **Compiler warnings**: 1 warning on clean build (down from 182 in April)
- **Binaries built**: `claw` (5.9M), `claw-lsp` (6.0M), `claw-repl` (5.8M)
- **Missing binary**: `claw-debugger` (not built by default)
- **CI/CD**: GitHub Actions with macOS + Linux build + test + benchmark; release workflow exists

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

1. **Bytecode VM**: repeated "No main function found in bytecode module" errors during test runs
2. **Missing test binaries**: `test_auto_scheduler`, `test_debugger`, `test_wasm_ir` not compiled
3. **AOT linker warning**: `ld: warning: no platform load command found in '*.o', assuming: macOS`
4. **Coverage**: `make coverage` fails because `lcov` is not installed
5. **Bytecode mode**: `--mode=bytecode` only runs parse+typecheck, does not execute
6. **Struct constructors in expressions**: `Point(1, 2)` works in `let` initializer but not as general expression (e.g., return value or method call arg)

## Execution Mode Verification

| Mode | Command | Status | Notes |
|------|---------|--------|-------|
| AST Interpreter | `--run` | works | basic scripts execute correctly; self receiver supported |
| C CodeGen | `-C` | works | generates C code |
| AOT Native | `--aot` | works | produces runnable Mach-O binary |
| Bytecode VM | `--mode=bytecode` | partial | compiles but does not execute |
| JIT | `--mode=jit` | unverified | needs validation |

## Recently Completed

- **Self receiver syntax**: `obj.method()` parses and executes correctly; `self` is injected as first argument for impl methods
- **Implicit returns**: function and impl method bodies now capture the last expression value as implicit return (matching lambda behavior)
- **Release automation**: release.yml workflow builds, packages, and publishes macOS/Linux tar.gz + Docker image on tag push

## Production Deployment Readiness

| Dimension | Score | Notes |
|-----------|-------|-------|
| Language core | 85% | rich feature set, generics, pattern matching, effects, self receiver |
| Compiler backends | 80% | C/LLVM/AOT/Bytecode/JIT/WASM emitters exist |
| Optimizer pipeline | 90% | 13+ passes, -O0/-O1/-O2/-O3, iterative convergence |
| Execution engines | 60% | interpreter solid; bytecode/VM/JIT need stabilization |
| Developer tools | 55% | LSP/REPL built but unverified; debugger not built |
| Package manager | 80% | full manifest/resolve/lock/cache implementation |
| Test coverage | 65% | many unit tests, missing integration tests & some binaries |
| Build system | 85% | robust Makefile, auto-detection, install target |
| CI/CD | 65% | build+test+benchmark on macOS/Linux; release workflow exists but no Windows; no artifact signing |
| Documentation | 55% | extensive design docs, lacking user tutorials |
| Code quality | 90% | only 1 compiler warning on clean build |
| Release packaging | 60% | tar.gz + Docker + Homebrew formula; missing Windows installer, deb/rpm, checksums/signatures |
| **Overall** | **~72%** | core compiler is strong; execution stability and release tooling are the main gaps |

## Deployment Gaps (Critical to Address)

1. **Release workflow copies unbuilt binary**: `release.yml` packages `claw-debugger` which is not in `make all`
2. **No Windows CI**: only macOS and Linux; Windows is a major platform gap
3. **Coverage broken**: `make coverage` fails; no coverage reporting in CI
4. **No artifact signing**: release tar.gz lacks SHA256 checksums and GPG signatures
5. **Bytecode/VM not executing**: `--mode=bytecode` does not run programs
6. **Debugger unbuilt**: `claw-debugger` source exists but not compiled by default
