# Claw 编译器

> **确定性内存管理 · 零 GC 停顿 · 编译器全权负责生命周期**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CI](https://github.com/yourusername/claw-compiler/actions/workflows/ci.yml/badge.svg)](https://github.com/yourusername/claw-compiler/actions/workflows/ci.yml)

---

## 核心思想

Claw 的内存哲学只有一句话：**程序员自由定义变量，编译器负责分配和回收，没有垃圾收集器。**

```
程序员写的          编译器生成的
─────────          ──────────
let s = "hello"    char* s = claw_alloc("hello")
return s           // skip-free: s → 所有权转移给调用者
                   return s;

// 离开 scope 时    claw_free(s)  // 自动插入
```

| 场景 | 所有权去向 | 清理责任 |
|------|-----------|---------|
| scope 正常退出 | 无 | **当前 scope** 自动 free |
| `return x` | 转移给调用者 | **调用者 scope** 负责 free |
| `throw e` | 转移给 catch | **catch scope** 负责 free |

**未来升级路径**：`claw_alloc` / `claw_free` 是统一 hook 点，替换为对象池只需改一行：
```
claw_alloc → pool_acquire
claw_free  → pool_release
```

---

## 架构总览

```
源码 (.claw)
    │
    ▼
┌─────────┐
│  Lexer   │  token.h + lexer.h
│  词法分析 │  关键字/标识符/字面量/运算符
└────┬─────┘
     │ Token 流
     ▼
┌─────────┐
│  Parser  │  parser.h
│  语法分析 │  递归下降，生成 AST
└────┬─────┘
     │ AST
     ▼
┌─────────────────────────────────┐
│  Type Checker + Semantic        │
│  类型检查 + 语义分析              │
└────┬────────────────────────────┘
     │
     ▼
┌─────────────────────────────────┐
│  IR Generator                   │
│  AST → SSA-based IR             │
└────┬────────────────────────────┘
     │
     ├───→ C Codegen (-C)
     ├───→ Native AOT (--aot) → x86-64 Mach-O 可执行文件
     ├───→ LLVM IR (-c)
     ├───→ WASM (后端开发中)
     └───→ Bytecode Compiler ──→ ClawVM / JIT
```

---

## 快速开始

### 依赖

- **clang++** (C++17)
- **LLVM** (自动通过 `llvm-config` 探测路径)
- **libmsquic** (可选，启用 WebTransport 支持)
- **libreadline**

### 源码编译

```bash
cd claw-compiler
make all          # 编译 claw + claw-lsp + claw-repl
```

Makefile 会自动探测 LLVM 路径（通过 `llvm-config`）。如果探测失败，可手动指定：

```bash
make all LLVM_PREFIX=/usr/local/opt/llvm
```

禁用 WebTransport（无需 libmsquic）：

```bash
make all CLAW_ENABLE_WEBTRANSPORT=0
```

### 安装与卸载

```bash
make install              # 安装到 /usr/local/bin
make install PREFIX=/opt  # 自定义安装路径
make uninstall            # 移除已安装的二进制文件
```

### Docker

```bash
docker build -t claw .
docker run --rm -v $(pwd):/src claw --run /src/program.claw
```

### Homebrew (草稿)

```bash
brew tap yourusername/claw
brew install claw --with-libmsquic   # 启用 WebTransport
brew install claw                    # 禁用 WebTransport
```

### 使用

```bash
# 解释器模式 — 直接运行
./claw --run program.claw

# C 代码生成
./claw -C program.claw > program.c
gcc program.c -o program

# AOT 编译为原生可执行文件 (x86-64)
./claw --aot -o myapp program.claw
./myapp

# 字节码模式
./claw --mode=bytecode program.claw

# JIT 模式
./claw --mode=jit program.claw

# REPL 交互式环境
./claw-repl
```

### 示例

```claw
fn fibonacci(n: i64) -> i64 {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

fn main() {
    println(fibonacci(10))  // 55
}
```

---

## 已实现特性

### 语言核心
- **函数定义**：`fn name(param: type) -> type { ... }`
- **变量声明**：`let x: i64 = 0`，支持 const
- **控制流**：`if/else`、`for i in range`、`while`
- **循环控制**：`break`、`continue`、`return`
- **表达式**：算术、比较、逻辑、位运算
- **数据类型**：i8-i64、u8-u64、f32/f64、bool、string、数组 `T[N]`

### 执行引擎 (5 种模式)
| 模式 | 命令 | 说明 |
|------|------|------|
| AST 解释器 | `--run` | 树遍历解释器 |
| C 代码生成 | `-C` | 生成带生命周期标注的 C 代码 |
| LLVM IR | `-c` | 生成 LLVM IR |
| AOT 编译 | `--aot` | AST → Bytecode → x86-64 → Mach-O 可执行文件 |
| Bytecode VM | `--mode=bytecode` | ClawVM 栈式虚拟机 |
| JIT | `--mode=jit` | Method JIT + Optimizing JIT + Tracing JIT |

### AOT 编译
- AST → Bytecode → x86-64 机器码 → Mach-O → 原生可执行文件
- 支持算术、比较、逻辑运算、条件分支、循环、函数调用、递归
- 自动链接 AOT 运行时 stub (print/println)
- 智能构建缓存 (FNV-1a 哈希，重复构建 ~0.5ms)
- 自动探测最快系统链接器 (zld/mold/lld)

### JIT 编译器
- **Method JIT**: 方法级快速编译
- **Optimizing JIT**: 常量折叠、DCE、强度消减、函数内联、循环优化
- **Tracing JIT**: 热点轨迹记录与编译
- **多目标支持**: x86-64 / ARM64 / RISC-V64
- **线性扫描寄存器分配器**

### 包管理器
- `Claw.toml` 清单解析 (TOML-like 语法)
- SemVer 版本解析与约束匹配 (`^`, `~`, `>=`, 范围)
- 依赖解析器 (回溯算法 + 约束满足)
- 锁定文件 (`Claw.lock`) 与拓扑排序
- 本地缓存管理 (LRU)

### 开发工具
- **LSP 服务器** (`claw-lsp`): 补全、跳转、重命名、语义高亮
- **REPL** (`claw-repl`): 多行输入、变量存储、历史记录
- **调试器** (`claw-debugger`): 断点、单步、调用栈、变量查看
- **性能测量框架**: 11 种指标、5 种输出格式 (text/csv/json/markdown/html)

### 张量优化
- **TensorIR**: 张量中间表示 + 调度原语 (tile/fuse/split/reorder/parallel/unroll/vectorize)
- **Auto-Scheduler**: 随机搜索 + 进化算法
- **CUDA 代码生成器**: 生成可编译的 CUDA C++ 代码

### 属性与宏系统
- Rust 风格属性: `#[inline]`, `#[target(arch = "cuda")]`
- 17 种内置属性
- 对象式/函数式宏 + 递归展开

### 模式匹配与穷尽性检查
- 专用 `Pattern` AST 层级（wildcard、variable、literal、constructor、tuple、array、or、range、binding）
- 基于 Wadler/Leijen 算法的穷尽性检查
- `match` 语句编译期覆盖验证，报告未覆盖分支

### 泛型单态化（零开销泛型）
- 编译期泛型实例化：`fn<T> id(x: T) -> T` → `id__Int`、`id__String`
- 实参类型推断泛型参数（`id(42)` 自动推断 `T = Int`）
- 实例缓存去重，避免代码膨胀

### 精准错误处理（Error Effect Tracking）
- 函数签名标注 `raise` / `noraise` / `raise?`
- 编译期错误效应推导与传播检查
- `try?` 表达式编译期脱糖为 `Result<T, E>`
- 高阶函数错误多态性

### 零开销迭代器
- `for x in arr` 编译期脱糖为索引循环，消除 IteratorValue 堆分配
- `for i in start..end` 范围迭代脱糖
- `for x in enumerate(arr)` 枚举迭代脱糖
- 脱糖后字节码与手写循环指令数一致（48 vs 48，0% 差异）

### AI 原生设计
- 结构化诊断输出（`--diagnostics-json`）：错误码、源码位置、修复建议
- 增强类型推断： Hindley-Milner 风格泛型参数推导
- 紧凑 AST 序列化（S-表达式风格），节省 30-50% tokens

---

## 项目结构

```
claw-compiler/
├── src/
│   ├── main.cpp                    # CLI 入口 (--run / -C / --aot / --mode=...)
│   ├── lexer/                      # 词法分析
│   ├── parser/                     # 语法分析
│   ├── ast/                        # AST 节点
│   ├── type/                       # 类型系统 + 张量推断
│   ├── semantic/                   # 语义分析器
│   ├── ir/                         # SSA-based IR + 优化器
│   ├── bytecode/                   # 字节码编译器 + 指令集
│   ├── vm/                         # ClawVM 栈式虚拟机
│   ├── jit/                        # JIT 编译器 + 运行时
│   ├── codegen/                    # AOT / Native / Mach-O / Linker
│   ├── emitter/                    # x86-64 / ARM64 / RISC-V / WASM 发射器
│   ├── pipeline/                   # 执行引擎 + 性能分析
│   ├── package/                    # 包管理器 (manifest/resolve/lock)
│   ├── lsp/                        # LSP 服务器
│   ├── repl/                       # REPL
│   ├── debugger/                   # 调试器
│   ├── benchmark/                  # 性能测量框架
│   ├── tensorir/                   # TensorIR
│   ├── auto_scheduler/             # 自动调度系统
│   ├── ml/                         # ML 成本模型
│   ├── backend/                    # CUDA 代码生成
│   ├── frontend/                   # 属性/宏系统
│   ├── json/                       # JSON 序列化
│   ├── stdlib/                     # 标准库集成
│   └── common/                     # 公共模块
├── tests/                          # 单元测试
├── Makefile                        # 主构建系统 (无 CMake)
├── claw-memory-model.md            # 内存所有权模型规范
└── dev_status.md                   # 详细开发日志
```

---

## 测试

```bash
make test              # 运行所有可用测试
make test-benchmark    # 性能测量框架 (20 测试)
make test-cuda         # CUDA 代码生成 (17 测试)
make test-package      # 包管理器 (24 测试)
make test-attribute    # 属性/宏系统 (17 测试)
make test-docgen       # 文档生成器 (16 测试)
make test-ir-passes    # IR 优化遍
make test-lexer        # 词法分析器 (29 测试)
make test-aot                 # AOT 端到端测试 (17 测试)
make test-pattern-checker     # 模式匹配穷尽性检查
make test-monomorphizer       # 泛型单态化
make test-iterator-desugarer  # 迭代器脱糖
make test-iterator-benchmark  # 零开销迭代器性能验证
make test-type-inference      # 类型推断增强
make test-compact-ast         # 紧凑 AST 序列化
```

---

## 开发计划

### 已完成
- [x] Phase 1: 核心前端 (Lexer + Parser + AST)
- [x] Phase 2: 执行引擎 (解释器 + C 代码生成)
- [x] Phase 3: 类型系统 + 语义分析
- [x] Phase 4: 编译流水线集成 (IR + LLVM + 优化器)
- [x] Phase 8: ClawVM + 字节码 + IR/Bytecode 桥接
- [x] Phase 9: JIT 编译器 (Method + Optimizing + Tracing)
- [x] Phase 10: TensorIR + 自动调度
- [x] Phase 12: LSP 服务器
- [x] Phase 13: 编译器核心完善
- [x] Phase 14: RISC-V JIT
- [x] Phase 15: 调试器
- [x] Phase 16: 高级优化框架
- [x] Phase 17: ML 特征提取
- [x] Phase 18: JIT 基础设施 (TypeProfiler/InlineCache/HotSpot)
- [x] Phase 19-21: Method JIT / 迭代器 / Tracing JIT
- [x] Phase 22: Native Codegen
- [x] Phase 23: WebAssembly 后端 (核心完成)
- [x] Phase 24: CUDA 代码生成器
- [x] Phase 26: 属性与宏系统
- [x] Phase 27: 文档生成器
- [x] Phase 28: 模式匹配增强 + 穷尽性检查
- [x] Phase 29: 泛型单态化（Monomorphization）
- [x] Phase 30: 精准错误处理（Error Effect Tracking）
- [x] Phase 31: 零开销迭代器（编译期脱糖）
- [x] Phase 32: AI 原生设计（结构化诊断 / 类型推断 / 紧凑 AST）

### 进行中 / 待完善
- [ ] WebAssembly 后端完整编译链
- [ ] 结构体 / 枚举
- [ ] 标准库泛型容器（`Array<T>`、`Map<K,V>` 完整后端支持）
- [ ] 效果系统与异步/并发模型

---

## 许可证

MIT License - 详见 [LICENSE](./LICENSE)
