# Current State

<!-- brain:begin context-current-state -->
This file is a deterministic snapshot of the repository state at the last refresh.

## Repository

- Project: `claw-compiler`
- Root: `.`
- Runtime: `C++17 / clang++ / LLVM 19.1.4`
- Current branch: `main`
- Remote: `https://github.com/iamkarlkm/claw-compiler-cpp.git`
- Build system: `Makefile` (primary) + `CMakeLists.txt` (secondary)
- Test framework: none yet

## Build Status

- **`make claw`**: ✅ PASSING (0 errors, 0 linker errors, 17MB binary)
- Last successful build: 2026-04-29 15:06
- Only warnings remain: format specifiers in tracing_jit.cpp, switch enum in schedule_space.cpp

## Docs

- `README.md`
- `docs/claw-comment-syntax.md`
- `docs/claw-compiler-design.md`
- `docs/claw-event-system.md`
- `docs/claw-language-spec.md`
- `docs/claw-memory-model.md`
- `docs/claw-minimal-type-system.md`
- `docs/claw-ml-compiler-integration.md`
- `docs/claw-naming-model.md`
- `docs/claw-runtime-architecture.md`
- `docs/claw-syntax-update.md`
- `docs/claw-tensor-optimization.md`
- `docs/claw-tensor-quickstart.md`
- `docs/claw-type-system.md`
- `docs/project-architecture.md`
- `docs/project-overview.md`
- `docs/project-workflows.md`
- `docs/self-modification-plan.md`

## IR Optimizer Progress

### Completed (Session 2026-04-29)
- [x] CMakeLists.txt: ir_optimizer.cpp + 24 missing source files added to CLAW_SOURCES
- [x] Makefile: ir_optimizer.cpp already integrated (src/ir/ir_optimizer.cpp line 35)
- [x] 10 passes implemented: ConstantFolding, ConstantPropagation, DCE, LocalCSE, StrengthReduction, LICM, SimplifyControlFlow, SimplifyPHIs, Inlining, DeadStoreElimination
- [x] PassManager with OptLevel O0-O3 and fixed-point iteration
- [x] Convenience entry: optimize_module() / optimize_function()

### In Progress
- [ ] DynamicCastInst + InstanceofInst added to ir.h OpCode + struct definitions
- [ ] IRBuilder methods: create_dynamic_cast / create_instanceof added to ir.h
- [ ] GlobalCSEPass declaration added to ir_optimizer.h — .cpp implementation pending
- [ ] BasicAliasAnalysisPass declaration added to ir_optimizer.h — .cpp implementation pending
- [ ] IRVerifierPass declaration added to ir_optimizer.h — .cpp implementation pending

### Pending
- [ ] Compile verification after all new passes implemented
- [ ] Unit tests for new passes
<!-- brain:end context-current-state -->
