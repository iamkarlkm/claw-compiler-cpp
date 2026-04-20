# Claw Compiler Architecture

## Multi-Layer Compilation Pipeline

### Layer 1: Hardware Target Compiler
- **Target**: Bare metal, embedded systems, FPGA
- **Output**: Machine code with zero runtime dependencies
- **Features**: Direct memory mapping, interrupt handling, register manipulation
- **Safety**: Compile-time memory safety guarantees, no undefined behavior

### Layer 2: Virtual Hardware Abstraction  
- **Target**: Virtual machines, containers, cloud instances
- **Output**: Portable bytecode with hardware abstraction layer (HAL)
- **Features**: Resource virtualization, cross-platform compatibility
- **Safety**: Capability-based security, resource isolation

### Layer 3: Kernel Mode Compiler
- **Target**: Operating system kernels, drivers, system services  
- **Output**: Kernel modules with minimal trusted computing base
- **Features**: System call interfaces, memory management, process scheduling
- **Safety**: Formal verification support, privilege separation

### Layer 4: Shell/CLI Compiler
- **Target**: Command-line interfaces, scripting environments
- **Output**: Efficient bytecode with rich I/O capabilities  
- **Features**: Pipeline composition, interactive debugging, job control
- **Safety**: Input validation, command injection prevention

### Layer 5: UI Framework Compiler
- **Target**: Desktop, mobile, web user interfaces
- **Output**: Optimized rendering code with reactive data binding
- **Features**: Declarative UI, state management, animation system
- **Safety**: XSS prevention, secure component isolation

## Unified Type System
- **Zero-cost abstractions**: Generic programming without runtime overhead
- **Algebraic data types**: Enums with associated data for error handling
- **Linear types**: Guaranteed resource cleanup and ownership tracking
- **Effect system**: Explicit side effect tracking and capability management

## Memory Management
- **Compile-time ownership**: Rust-like borrow checker with improved ergonomics
- **Region-based allocation**: Stack-like allocation for predictable performance  
- **Automatic reference counting**: For shared data structures with cycle detection
- **Manual control**: Unsafe blocks for performance-critical sections

## Concurrency Model
- **Async/await**: Built-in asynchronous programming with zero-cost futures
- **Actor model**: Message-passing concurrency with guaranteed message ordering
- **Shared state**: Lock-free data structures with atomic operations
- **Parallel execution**: Data parallelism with automatic work stealing

## Interoperability
- **C FFI**: Seamless integration with existing C/C++ codebases
- **WebAssembly**: Compile to WASM for web deployment
- **Python/Rust/Go bindings**: Native interop with popular languages
- **Hardware description**: Generate Verilog/VHDL for FPGA targets

## Toolchain Components
1. **clawc**: Main compiler with multi-target support
2. **clawfmt**: Code formatter with configurable style rules  
3. **clawlsp**: Language server for IDE integration
4. **clawtest**: Built-in testing framework with property-based testing
5. **clawdoc**: Documentation generator with examples and tutorials
6. **clawpkg**: Package manager with dependency resolution and security auditing

## Execution Engine Architecture (2026-04-17 新增)

### 多模式执行引擎
Claw 编译器支持 4 种执行模式，共享前端 (Lexer/Parser/AST)，按需选择后端:

```
Claw 源码 → Lexer → Parser → AST
       │
       ├─[解释模式]→ AST Interpreter (当前实现)
       │
       ├─[字节码模式]→ BytecodeCompiler → ClawVM (Stack-based)
       │
       ├─[JIT 模式]→ BytecodeCompiler → 热点检测 → IR Lifter
       │              → JIT Optimizer → Machine Code Emitter
       │                                            ↗ x86-64
       │                                           → ARM64
       │                                           → RISC-V
       │
       └─[AOT 模式]→ IR Generator → IR Optimizer
                      ├─[自研后端]→ Machine Code Emitter
                      └─[LLVM后端]→ LLVM Codegen → LLVM Opt → 目标代码
```

### Claw 字节码 (ClawBytecode)
- **类型**: 栈式字节码 (Stack-based Bytecode)
- **指令集**: ~60 条指令 (算术/比较/逻辑/控制流/函数/数组/张量)
- **文件格式**: .cbc 二进制文件 (Magic Number + 版本 + 常量池 + 代码段 + 调试段)
- **常量池**: 字符串、浮点数、大整数池化，减少内存占用
- **参考**: Lua 5.x 字节码格式 (紧凑、高效)

### Claw 虚拟机 (ClawVM)
- **架构**: 栈式虚拟机 (Value Stack + Call Frame Stack)
- **调度**: switch-dispatch (兼容) / computed-goto (GCC/Clang, 快 15-25%)
- **GC**: Mark-Sweep → 分代 GC → 写屏障 (三阶段演进)
- **性能目标**: 比 AST 直译快 5-10 倍

### JIT 编译器
- **Method JIT (基线)**: 字节码→机器码 1:1 翻译, <1ms/函数
- **Optimizing JIT**: 类型特化, 内联, 逃逸分析, LICM, DCE
- **Tracing JIT (可选)**: LuaJIT 风格 trace compiler
- **性能目标**: 比 VM 解释快 10-50 倍, 达到 C/C++ 50-80%

### 多目标机器码生成
- **x86-64**: System V AMD64 ABI, SSE2/AVX 浮点, PIC 支持
- **ARM64 (AArch64)**: AAPCS64 ABI, NEON 浮点, 定长 4 字节指令
- **RISC-V (RV64IMFD)**: RISC-V ABI, M+F+D 扩展, 定长 4 字节指令
- **寄存器分配**: Linear Scan (JIT 场景), 图着色 (AOT 场景)
- **参考**: LLVM MC 层 + LuaJIT DynASM

### 字节码 ↔ IR 桥接
- **IR → Bytecode**: SSA→栈式转换, PHI 消除, 基本块→偏移量映射
- **Bytecode → IR**: 热点检测触发, 字节码块提升为 IR 函数, 供 JIT 优化
- **混合执行**: 冷路径 VM 解释, 热路径 JIT 编译

## Safety Guarantees
- **Memory safety**: No buffer overflows, use-after-free, or null pointer dereferences
- **Thread safety**: Data race prevention through ownership and borrowing
- **Type safety**: Strong static typing with type inference
- **Security**: Capability-based access control, input sanitization, secure defaults