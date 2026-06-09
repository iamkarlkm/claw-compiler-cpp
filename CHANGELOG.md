# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- CI pipeline via GitHub Actions (macOS + Linux + Windows).
- Coverage reporting via `make coverage` with lcov 2.x.
- Release workflow with GPG-signed artifacts and Docker images.
- `LICENSE` file (MIT).
- `VERSION` and `CHANGELOG.md` for release tracking.

### Fixed
- AOT linker warning on macOS by adding `-mmacosx-version-min=10.15` to runtime stub compilation.
- Debugger segfault on exit by clearing event callback before member destruction.
- JIT `main` returning garbage due to missing `pop rax` in `emit_return_op` and void stdlib builtins.
- Bytecode compiler `return;` emitting incorrect double `PUSH` instructions; now emits `RET_NULL`.
- Bytecode implicit return logic now correctly handles `RET_NULL`.
- Makefile linker errors for `claw-repl` and `claw-lsp` by linking full core objects.
- CLI `--mode=bytecode` argument parsing failed to recognize `--mode=xxx` syntax; now matches `--mode=` prefix correctly.
- VM `print`/`println` quoting strings with `"..."` while AST interpreter printed raw strings; added `to_print_string()` for user-facing output.
- AST interpreter missing array/string builtins (`arr_range`, `arr_len`, `arr_push`, `str_len`, `str_upper`, `str_contains`).

## [0.2.0]

### Added
- **Implicit generic inference**: calls like `id(42)` are automatically resolved to `id<Int>(42)`.
- **Enhanced diagnostics system**: `ParserRecoveryHelper`, structured fixits, source reader, severity filtering, and JSON/Markdown/plain formatters.
- **Compact AST representation**: `--compact-ast` CLI mode for AI-friendly AST output.
- **WebTransport backend**: msquic-based WebTransport support in the VM.
- **AOT compilation**: AST → Bytecode → x86-64 → Mach-O native executable.
- **JIT compiler**: Method JIT, Optimizing JIT, and Tracing JIT with x86-64/ARM64/RISC-V64 support.
- **Bytecode VM**: `ClawVM` stack-based virtual machine with optimization passes.
- **Zero-cost iterators**: `for x in arr` desugars to index loops with identical instruction count to hand-written loops.
- **Error effect tracking**: `raise`/`noraise`/`raise?` annotations with compile-time propagation checking.
- **Pattern matching**: exhaustive match checking via Wadler/Leijen algorithm.
- **Package manager**: `Claw.toml` manifest, SemVer resolution, lock files.
- **LSP server** (`claw-lsp`): completion, goto-definition, rename, semantic tokens.
- **REPL** (`claw-repl`): multi-line input, variable storage, history.
- **Debugger** (`claw-debugger`): breakpoints, stepping, call stack inspection.
- **TensorIR + Auto-Scheduler**: tensor scheduling primitives and evolutionary search.
- **CUDA codegen**: generates compilable CUDA C++ kernels.
- **Attributes/macros**: 17 built-in attributes and recursive macro expansion.

### Changed
- Major Makefile refactoring to support WebTransport and multi-target builds.

### Fixed
- Iterator desugaring `len()` call generation.
- Test linker errors for iterator benchmarks.

## [0.1.0]

### Added
- Initial compiler pipeline: Lexer → Parser → AST → Type Checker.
- Core language: functions, variables, control flow, arithmetic/logic expressions.
- Basic interpreter and C code generation.
