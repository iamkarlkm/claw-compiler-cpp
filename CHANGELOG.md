# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- CI pipeline via GitHub Actions (macOS + Linux).
- `LICENSE` file (MIT).
- `VERSION` and `CHANGELOG.md` for release tracking.

### Fixed
- Makefile linker errors for `claw-repl` and `claw-lsp` by linking full core objects.

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
