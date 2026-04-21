# Claw 编译器

> **确定性内存管理 · 零 GC 停顿 · 编译器全权负责生命周期**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

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
│  Lexer   │  token.h + lexer.h (1,081 行)
│  词法分析 │  关键字/标识符/字面量/运算符
└────┬─────┘
     │ Token 流
     ▼
┌─────────┐
│  Parser  │  parser.h (1,988 行)
│  语法分析 │  递归下降，生成 AST
└────┬─────┘
     │ AST
     ▼
┌─────────────────────────────────┐
│          AST (ast.h, 910 行)      │
│  Expression: Literal/Id/Binary/  │
│    Unary/Call/Index/Array/Range  │
│  Statement: Let/If/For/While/    │
│    Return/Block/Function/        │
│    Try/Catch/Throw (进行中)       │
└──┬──────────────────────────┬───┘
   │                          │
   ▼                          ▼
┌──────────┐          ┌──────────┐
│解释器执行  │          │ C 代码生成 │
│interpreter│          │c_codegen  │
│(1,500 行) │          │(1,073 行) │
│--run 模式 │          │-C 模式    │
└──────────┘          └──────────┘
```

---

## 快速开始

### 编译

```bash
cd claw-compiler
make claw          # 需要 clang++ + C++17
```

### 使用

```bash
# 解释器模式 — 直接运行
./claw --run program.claw

# C 代码生成
./claw -C program.claw > program.c
gcc program.c -o program
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

### For 循环语义
```claw
for i in 5      // 1..5，Claw 1-based range
for i in 1..5   // 显式范围
for i in arr    // 遍历数组
```

### 递归支持
- 解释器通过 `variable_stack` + `scoped_set/scoped_get` 实现帧隔离
- `return_value` / `return_flag` 在函数调用时 save/restore
- 验证通过：fib(15)=610, factorial(10)=3628800

### C 代码生成 — 统一生命周期管理
生成的 C 代码带有完整的 scope 标注：

```c
int64_t test_scope() {
    // [claw] scope-enter: fn_body (depth=1)
    int64_t x = 0;
    // [claw] scope-enter: block_1 (depth=2)
    char* s = "hello"; // [claw] alloc: s (string)
    if ((x == 1)) {
        // [claw] pre-return cleanup: block_1
        // [claw] skip-free: s (is return value)
        return s;
    }
    // [claw] scope-free: block_1 (1 heap var(s))
    claw_free((void*)s); // [claw] free: s
    // [claw] scope-exit: block_1
    return 0;
} // [claw] scope-exit: fn_body
```

---

## 进行中

- **try/catch/throw 异常处理**
  - ✅ Token 枚举 + Lexer 关键字
  - ✅ AST 节点 (TryStmt / CatchClause / ThrowStmt)
  - 🔲 Parser 规则
  - 🔲 Interpreter 异常栈展开
  - 🔲 C Codegen setjmp/longjmp

---

## 项目结构

```
claw-compiler/
├── src/
│   ├── main.cpp                    # 入口：--run 解释器 / -C 代码生成
│   ├── lexer/
│   │   ├── token.h                 # Token 枚举 + 关键字映射
│   │   └── lexer.h                 # 词法分析器
│   ├── ast/
│   │   └── ast.h                   # AST 节点定义
│   ├── parser/
│   │   └── parser.h                # 递归下降解析器
│   ├── interpreter/
│   │   └── interpreter.h           # 树遍历解释器
│   ├── codegen/
│   │   └── c_codegen.h             # C 代码生成器
│   ├── type/                       # 类型系统（开发中）
│   ├── semantic/                   # 语义分析（开发中）
│   └── tensorir/                   # 张量 IR（规划中）
├── Makefile
├── claw-memory-model.md            # 内存所有权模型规范
└── dev_status.md                   # 开发日志
```

### 核心代码量

| 模块 | 文件 | 行数 |
|------|------|------|
| Lexer | token.h + lexer.h | 1,081 |
| AST | ast.h | 910 |
| Parser | parser.h | 1,988 |
| Interpreter | interpreter.h | 1,500 |
| C Codegen | c_codegen.h | 1,073 |
| Main | main.cpp | 246 |
| **总计** | | **6,798** |

---

## 开发计划

### Phase 1: 核心前端 ✅
- [x] Lexer (词法分析器)
- [x] Parser (递归下降)
- [x] AST (完整节点体系)

### Phase 2: 执行引擎 🔄
- [x] 树遍历解释器 (--run)
- [x] C 代码生成 (-C)
- [x] 统一 scope 生命周期管理
- [x] 递归函数调用
- [ ] try/catch/throw 异常处理

### Phase 3: 类型系统 📋
- [ ] 类型检查器
- [ ] 泛型支持
- [ ] 结构体 / 枚举

### Phase 4: 优化与后端 📋
- [ ] IR 中间表示
- [ ] 常量折叠 / 死代码消除
- [ ] LLVM 后端（可选）
- [ ] 对象池分配器

---

## 许可证

MIT License - 详见 [LICENSE](./LICENSE)
