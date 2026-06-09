# Python vs Claw 语法对照表与开发计划

> 生成时间：2026-05-30
> 目标：系统性对比 Python 语法糖与 Claw 语言现状，制定增量开发计划

---

## 一、总体评估

| 维度 | Python | Claw |
|------|--------|------|
| 类型系统 | 动态类型 + 可选 Type Hints | 静态类型 + 泛型 + 类型推断 |
| 执行模型 | 解释执行 / 字节码 VM | 编译为字节码 + AOT/JIT |
| 内存管理 | GC（引用计数） | 无 GC（仿 Rust 所有权，规划中） |
| 错误处理 | 异常（Exception） | 异常 + Error Effect + Result |
| 泛型 | 运行时擦除（typing.Generic） | 编译期单态化（零开销） |
| OOP | 类继承 + 多重继承 | Trait + Impl（无继承） |
| 迭代器 | 运行时协议（__iter__） | 编译期脱糖（零开销） |

**借鉴原则**：
1. **静态可编译**：Python 的动态特性（`**kwargs`、`getattr`、元类）不直接借用
2. **零开销抽象**：语法糖必须能在编译期完全脱糖，不产生运行时开销
3. **渐进引入**：每新增一个语法糖，配套 parser → AST → typecheck → bytecode → test 全链路

---

## 二、语法对照详表

### 2.1 基础语法

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 1 | 变量绑定 | `x = 42` | 已有 | `let x = 42` / `let mut x = 42` | 已实现 | N/A | Claw 显式区分 imm/mut |
| 2 | 链式赋值 | `a = b = c = 0` | 无 | 无 | 未实现 | **低** | 静态语言中易与引用语义混淆 |
| 3 | 元组解包 | `a, b = (1, 2)` | 已有 | `let (a, b) = expr` | 已实现 | N/A | 两者等价 |
| 4 | 扩展解包 | `a, *rest = seq` | 无 | 无 | 未实现 | **中** | 需支持变长数组模式匹配 |
| 5 | 复合赋值 | `x += 1` | 已有 | `x += 1` | 已实现 | N/A | 完整支持 `+= -= *= /= &= |= ^=` |
| 6 | `global` | `global x` | 无 | `mod` 级静态变量 | 部分实现 | N/A | Claw 用模块系统替代 |
| 7 | `nonlocal` | `nonlocal x` | 无 | 闭包捕获 | 未实现 | **低** | 计划用 `ref` 捕获语义替代 |
| 8 | `del` | `del x` | 无 | 无 | 未实现 | **低** | 静态语言用 RAII/drop 替代 |
| 9 | `pass` | `pass` | 无 | `{}` 空 block | 已实现 | N/A | Claw 空 block 即 `pass` |
| 10 | 命名约定 | `snake_case` / `PascalCase` | 已有 | `snake_case` / `PascalCase` | 惯例 | N/A | Claw 社区惯例与 Python 一致 |
| 11 | 表达式赋值（海象算符） | `if (n := len(a)) > 10:` | 无 | 无 | 未实现 | **高** | `if let n = foo() { ... }` 更优雅，见 #31 |

### 2.2 数据类型与字面量

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 12 | 整数 | `42`, `0xFF`, `1_000_000` | 已有 | `42`, `0xFF` | 已实现 | N/A | 下划线分隔符（PEP 515）未支持，建议添加 |
| 13 | 浮点数 | `3.14`, `1e-3` | 已有 | `3.14`, `1e-3` | 已实现 | N/A | — |
| 14 | 复数 | `3 + 4j` | 无 | 无 | 未实现 | **低** | 系统语言通常作为库类型 |
| 15 | 布尔 | `True` / `False` | 已有 | `true` / `false` | 已实现 | N/A | — |
| 16 | 空值 | `None` | 已有 | `null` / `Option<T>` | 已实现 | N/A | Claw 推荐 `Option<T>` 替代裸 null |
| 17 | 字符串 | `"hello"`, `'''multi'''` | 已有 | `"hello"` | 已实现 | N/A | 三引号多行字符串未支持 |
| 18 | 原始字符串 | `r"\n"` | 无 | 无 | 未实现 | **中** | 正则需要，建议 `r"..."` |
| 19 | 字节串 | `b"hello"` | 已有 | `byte` 类型 + 数组 | 已实现 | N/A | — |
| 20 | **F-字符串** | `f"value={x+1}"` | 无 | 无 | 未实现 | **高** | 极高频语法糖，可编译期脱糖为 `format` 调用 |
| 21 | 列表字面量 | `[1, 2, 3]` | 已有 | `[1, 2, 3]` | 已实现 | N/A | — |
| 22 | 元组字面量 | `(1, 2)` | 已有 | `(1, 2)` | 已实现 | N/A | — |
| 23 | 字典字面量 | `{"a": 1}` | 无 | 无 | 未实现 | **高** | `Map<K, V>` 需要字面量语法，建议 `{ "a": 1 }` |
| 24 | 集合字面量 | `{1, 2, 3}` | 无 | 无 | 未实现 | **中** | `Set<T>` 字面量，与空字典 `{}` 冲突需设计 |
| 25 | **列表推导式** | `[x*2 for x in items]` | 无 | 无 | 未实现 | **高** | 零成本抽象，可脱糖为 `for` 循环 |
| 26 | 字典/集合推导式 | `{k:v for k,v in d}` | 无 | 无 | 未实现 | **高** | 同 #25，一并实现 |
| 27 | 生成器表达式 | `(x*2 for x in items)` | 无 | 无 | 未实现 | **中** | 惰性求值，可编译为状态机 |
| 28 | 下划线数字分隔 | `1_000_000` | 无 | 无 | 未实现 | **中** | 提升可读性，parser 改动小 |

### 2.3 控制流

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 29 | `if/elif/else` | `if x: ... elif y: ... else: ...` | 已有 | `if x { ... } else if y { ... } else { ... }` | 已实现 | N/A | — |
| 30 | `for` 迭代 | `for x in iterable:` | 已有 | `for x in iterable { ... }` | 已实现 | N/A | 已做零成本脱糖 |
| 31 | `while` | `while x < 10:` | 已有 | `while x < 10 { ... }` | 已实现 | N/A | — |
| 32 | `break` / `continue` | `break` / `continue` | 已有 | `break` / `continue` | 已实现 | N/A | — |
| 33 | `match/case` | `match obj: case Point(x, y): ...` | 已有 | `match expr { pattern => ... }` | 已实现 | N/A | Claw 的 `match` 已支持模式、守卫、穷尽检查 |
| 34 | `if let` | `if match: ...`（无原生语法） | 无 | 无 | 未实现 | **高** | `if let Some(x) = opt { ... }` 是 Rust 核心语法糖，建议优先实现 |
| 35 | `while let` | 无 | 无 | 无 | 未实现 | **中** | `while let Some(x) = iter.next() { ... }` |
| 36 | `loop` + `else` | `for x in it: ... else: ...` | 无 | 无 | 未实现 | **低** | Python 独有模式，使用频率低 |

### 2.4 函数

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 37 | 函数定义 | `def foo(x): return x` | 已有 | `fn foo(x: i64) -> i64 { return x; }` | 已实现 | N/A | — |
| 38 | `return` | `return x` | 已有 | `return x;` | 已实现 | N/A | — |
| 39 | Lambda | `lambda x: x + 1` | 已有 | `fn(x: i64) -> i64 { x + 1 }` | 已实现 | N/A | Claw 语法稍冗长，但功能完整 |
| 40 | 轻量闭包 | `lambda x: x + 1` | 无 | 无 | 未实现 | **中** | Rust 风格 `|x| x + 1` 可显著减少噪音 |
| 41 | **装饰器 `@`** | `@cache` | 无 | 无 | 未实现 | **高** | 编译期属性宏，可映射到 `#[derive]` 风格 |
| 42 | 默认参数 | `def f(x=10):` | 已有 | `fn f(x: i64 = 10) { ... }` | 已实现 | N/A | — |
| 43 | `*args` | `def f(*args):` | 无 | 无 | 未实现 | **低** | 静态类型系统中可用切片/变参模板替代 |
| 44 | `**kwargs` | `def f(**kwargs):` | 无 | 无 | 未实现 | **低** | 动态特性，与静态类型冲突 |
| 45 | 仅限关键字参数 | `def f(*, x):` | 无 | 无 | 未实现 | **中** | API 清晰度极佳，建议 `fn f(*, x: i64)` |
| 46 | 仅限位置参数 | `def f(x, /):` | 无 | 无 | 未实现 | **低** | C-ABI 兼容有用，但 Claw 目前不需要 |
| 47 | 类型注解 | `def f(x: int) -> str:` | 已有 | `fn f(x: i64) -> string { ... }` | 已实现 | N/A | Claw 类型是核心语法，非注解 |
| 48 | 调用解包 | `func(*args, **kw)` | 无 | 无 | 未实现 | **低** | 动态特性，静态语言用结构体展开 |
| 49 | 泛型函数 | `def foo(x: T) -> T:` | 已有 | `fn foo<T>(x: T) -> T { ... }` | 已实现 | N/A | 已支持单态化 + 隐式推断 |

### 2.5 类与 OOP

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 50 | 类定义 | `class A:` | 已有（Struct） | `struct A { x: i64 }` | 已实现 | N/A | Claw 用 `struct` 替代 `class` |
| 51 | 继承 | `class B(A):` | 无 | 无 | 未实现 | **低** | Claw 哲学：用 Trait + 组合替代继承 |
| 52 | 构造方法 | `def __init__(self, x):` | 已有 | 结构体字面量 + impl | 已实现 | N/A | `Point { x: 1, y: 2 }` |
| 53 | 显式 `self` | `def meth(self):` | 部分 | `impl Point { fn distance(self) { ... } }` | 部分实现 | **高** | 需要 `self`  receiver 语法糖（`obj.method()`） |
| 54 | 类方法 | `@classmethod` | 无 | 无 | 未实现 | **低** | 可用模块级函数替代 |
| 55 | 静态方法 | `@staticmethod` | 无 | 无 | 未实现 | **低** | 同上 |
| 56 | **属性 `@property`** | `@property` | 无 | 无 | 未实现 | **高** | `get/set` 语法糖，建议 `#[property]` 属性宏 |
| 57 | `super()` | `super().method()` | 无 | 无 | 未实现 | **低** | 无继承即无 super |
| 58 | **数据类 `@dataclass`** | `@dataclass` | 无 | 无 | 未实现 | **高** | 等价 Rust `#[derive(Debug, Clone)]`，编译宏实现 |
| 59 | `__slots__` | `__slots__ = ('x',)` | 已有 | `struct` 天然固定字段 | 已实现 | N/A | Claw struct 即固定布局 |

### 2.6 错误处理

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 60 | `try/except` | `try: ... except ValueError:` | 已有 | `try { ... } catch e: Error { ... }` | 已实现 | N/A | — |
| 61 | `try/finally` | `try: ... finally: ...` | 已有 | `try { ... } finally { ... }` | 已实现 | N/A | — |
| 62 | `else` on `try` | `try: ... else: ...` | 无 | 无 | 未实现 | **低** | 使用频率极低 |
| 63 | `raise` | `raise ValueError("x")` | 已有 | `throw expr` / `raise expr` | 已实现 | N/A | — |
| 64 | `raise from` | `raise new_exc from old_exc` | 无 | 无 | 未实现 | **中** | 错误链跟踪，对调试重要 |
| 65 | **上下文管理器 `with`** | `with open(f) as fh:` | 无 | 无 | 未实现 | **高** | RAII 语法糖，可脱糖为 try/finally + 析构 |
| 66 | `try?` | 无（Rust 有 `?`） | 已有 | `try? expr` | 已实现 | N/A | 将 raise 转为 `Result<T, E>` |

### 2.7 模块与导入

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 67 | `import` | `import os` | 已有 | `use std::io` | 已实现 | N/A | Claw 用 `use` |
| 68 | `from ... import` | `from os import path` | 已有 | `use std::io::read` | 已实现 | N/A | — |
| 69 | `as` | `import numpy as np` | 已有 | `use std::io::read as r` | 已实现 | N/A | — |
| 70 | 相对导入 | `from . import mod` | 无 | 无 | 未实现 | **中** | `use super::module` 或 `use crate::foo` |
| 71 | `__all__` | `__all__ = ['x']` | 无 | `pub` 修饰符 | 已实现 | N/A | `pub` 即显式导出 |

### 2.8 高级特性

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 72 | 生成器 `yield` | `yield x` | 无 | 无 | 未实现 | **中** | 可编译为状态机，VM 需支持 suspend/resume |
| 73 | `yield from` | `yield from subgen()` | 无 | 无 | 未实现 | **低** | 生成器委托，可在 #72 之后实现 |
| 74 | `async/await` | `async def f(): await g()` | 已有 | `async fn f() { await g(); }` | 已实现 | N/A | — |
| 75 | `async for/with` | `async for x in ait:` | 无 | 无 | 未实现 | **中** | 异步迭代器/上下文管理器协议 |
| 76 | 变量注解 | `x: int = 5` | 已有 | `let x: i64 = 5` | 已实现 | N/A | — |
| 77 | 切片 `[start:stop:step]` | `lst[1:5:2]` | 已有 | `arr[1..5]` | 已实现 | N/A | Claw 用 `..` / `..=` 范围运算符 |
| 78 | 负索引 | `lst[-1]` | 无 | 无 | 未实现 | **低** | 静态语言中负索引易产生 bounds check 歧义 |
| 79 | 字符串插值 `f"{expr=}"` | `f"{expr=}"` | 无 | 无 | 未实现 | **高** | 同 #20，调试输出 `expr=值` 极其实用 |
| 80 | 类型别名 | `Point = tuple[int, int]` | 部分 | `type Point = (i64, i64)` | AST 有节点，parser 未实现 | **高** | `type` 关键字已保留，只需 parser 支持 |

### 2.9 标准库模式

| # | Python 特性 | Python 示例 | Claw 现状 | Claw 示例 | 实现状态 | 推荐度 | 说明 |
|---|------------|-------------|-----------|-----------|----------|--------|------|
| 81 | `len()` | `len(x)` | 已有 | `len(x)` | 已实现 | N/A | 内置函数 |
| 82 | `range()` | `range(10)` | 已有 | `0..10` | 已实现 | N/A | 范围运算符更简洁 |
| 83 | `enumerate()` | `enumerate(lst)` | 已有 | `for (i, x) in arr.enumerate() { ... }` | 已实现 | N/A | 迭代器方法 |
| 84 | `zip()` | `zip(a, b)` | 已有 | `for (x, y) in zip(a, b) { ... }` | 已实现 | N/A | — |
| 85 | `map()` / `filter()` | `map(fn, it)` | 已有 | 可用 `for` + 推导式替代 | 已实现 | N/A | 列表推导式更 Pythonic |
| 86 | `sorted()` / `reversed()` | `sorted(lst)` | 部分 | `arr.sort()` / `arr.reverse()` | 部分实现 | **中** | 需稳定排序算法实现 |
| 87 | `all()` / `any()` | `all(pred(x) for x in it)` | 无 | 无 | 未实现 | **中** | 短路求量词，标准库函数即可 |
| 88 | `sum()` / `min()` / `max()` | `sum(it)` | 无 | 无 | 未实现 | **中** | 标准库折叠函数 |
| 89 | `hasattr/getattr` | `getattr(obj, 'name')` | 无 | 无 | 未实现 | **低** | 动态反射，与静态类型冲突 |
| 90 | `isinstance()` | `isinstance(x, int)` | 无 | 无 | 未实现 | **低** | 静态语言用模式匹配替代 |

---

## 三、推荐度汇总与优先级排序

### 高优先级（High）—— 建议下一迭代实现

| 排名 | 特性 | 编号 | 理由 |
|------|------|------|------|
| 1 | **F-字符串 / 字符串插值** | #20, #79 | 极高频语法糖，编译期脱糖为 `format` 调用，改动面小（lexer + parser + 少量 AST），用户体验提升最大 |
| 2 | **`if let` / `while let`** | #34, #35 | Rust 核心语法糖，避免 `match` 嵌套，提升 Option/Result 可用性，parser 改动中等 |
| 3 | **字典字面量 + 推导式** | #23, #25, #26 | 列表/字典/集合推导式是 Python 标志性特性，可零成本脱糖为循环，大幅提升集合操作表达能力 |
| 4 | **数据类自动派生** | #58 | 等价于 Rust `#[derive]`，通过属性宏自动实现 `Clone`、`Debug`、`Eq` 等，减少样板代码 |
| 5 | **装饰器 `@` / 属性宏** | #41 | 为 #58 提供基础设施，同时可用于 `#[test]`、`#[inline]` 等编译期元编程 |
| 6 | **类型别名 `type`** | #80 | AST 已有节点，仅需 parser 支持，改动极小，对泛型代码可读性帮助大 |
| 7 | **属性 `get/set`（@property）** | #56 | 通过属性宏实现 `#[property(get, set)]`，避免显式 getter/setter 方法 |
| 8 | **`self` receiver 方法调用** | #53 | `obj.method()` 自动绑定 `self`，OOP 基础语法糖，当前需 `Point::method(obj)` |

### 中优先级（Medium）—— 建议第二迭代实现

| 排名 | 特性 | 编号 | 理由 |
|------|------|------|------|
| 9 | **生成器 `yield`** | #72 | 需要 VM 支持 suspend/resume 状态机，改动面大，但现代语言标配 |
| 10 | **上下文管理器 `with`** | #65 | RAII 语法糖，可脱糖为 try/finally，资源管理场景高频 |
| 11 | **原始字符串 `r"..."`** | #18 | 正则需要，parser 改动小 |
| 12 | **下划线数字分隔** | #28 | 提升可读性，parser 改动极小 |
| 13 | **轻量闭包 `\|x\| x+1`** | #40 | 减少 lambda 语法噪音，但当前 `fn(x) { x+1 }` 可用 |
| 14 | `async for` / `async with` | #75 | 异步生态完善需要 |
| 15 | 标准库量词/折叠函数 | #87, #88 | `all`, `any`, `sum`, `min`, `max` 等，纯库实现 |
| 16 | `raise from` 错误链 | #64 | 调试体验提升 |
| 17 | 扩展解包 `a, *rest = seq` | #4 | 模式匹配增强 |

### 低优先级（Low）—— 可选或暂不实现

| 编号 | 特性 | 理由 |
|------|------|------|
| #2 | 链式赋值 | 静态语义复杂，易与引用混淆 |
| #6, #7 | `global` / `nonlocal` | 模块系统 + 闭包捕获已覆盖 |
| #8 | `del` | RAII 替代 |
| #14 | 复数 | 库类型即可 |
| #43, #44 | `*args` / `**kwargs` | 动态特性，与静态类型冲突 |
| #46 | 仅限位置参数 | C-ABI 场景少 |
| #48 | 调用解包 | 动态特性 |
| #51 | 类继承 | Trait 组合已替代 |
| #54, #55 | 类方法/静态方法 | 模块函数替代 |
| #57 | `super()` | 无继承即无需求 |
| #62 | `else on try` | 使用频率极低 |
| #73 | `yield from` | 生成器之后再考虑 |
| #78 | 负索引 | bounds check 语义复杂 |
| #89, #90 | `hasattr` / `isinstance` | 动态反射，静态语言用模式匹配 |

---

## 四、开发计划

### Phase A：语法糖基础层（预计 2-3 周）

**目标**：实现高频、低改动面、高用户体验的语法糖

| 周次 | 任务 | 涉及的文件 | 测试要求 |
|------|------|-----------|----------|
| A-1 | **F-字符串 lexer/parser/AST** | `src/lexer/token.h`, `src/parser/parser.h`, `src/ast/ast.h`, `src/ast/ast.cpp` | `src/test/test_fstring.cpp` |
| A-1 | **下划线数字分隔符** | `src/lexer/lexer.h`（数字字面量解析） | 集成到 lexer test |
| A-2 | **`if let` / `while let` parser** | `src/parser/parser.h`, `src/ast/ast.h` | `src/test/test_if_let.cpp` |
| A-2 | **类型别名 `type`** | `src/parser/parser.h`（AST 节点已存在） | `src/test/test_type_alias.cpp` |
| A-3 | **原始字符串 `r"..."`** | `src/lexer/lexer.h` | `src/test/test_raw_string.cpp` |
| A-3 | **字典/集合字面量 parser** | `src/parser/parser.h`, `src/ast/ast.h` | `src/test/test_collection_literals.cpp` |

**里程碑 A**：`make test` 通过，新增 5+ 语法糖，配套单元测试全部通过

---

### Phase B：推导式与集合操作（预计 2-3 周）

**目标**：实现 Python 标志性的 comprehension 语法，零成本脱糖

| 周次 | 任务 | 涉及的文件 | 测试要求 |
|------|------|-----------|----------|
| B-1 | **列表推导式 `[expr for x in iter if cond]`** | `src/parser/parser.h`, `src/ast/ast.h`, `src/optimizer/list_comprehension_desugarer.cpp` (新建) | `src/test/test_list_comprehension.cpp` |
| B-2 | **字典推导式 `{k:v for ...}`** | 同上 | `src/test/test_dict_comprehension.cpp` |
| B-2 | **集合推导式 `{x for ...}`** | 同上 | `src/test/test_set_comprehension.cpp` |
| B-3 | **推导式嵌套 + 多 `for` 子句** | `src/optimizer/list_comprehension_desugarer.cpp` | 端到端 `.claw` 测试 |
| B-3 | **`self` receiver 语法糖** | `src/parser/parser.h`, `src/type/type_checker.cpp` | `src/test/test_method_call.cpp` |

**脱糖策略**：
```
[x*2 for x in arr if x > 0]
↓
{
  let _result = Array::new();
  for x in arr {
    if x > 0 { _result.push(x*2); }
  }
  _result
}
```

**里程碑 B**：推导式支持单/多 `for` + `if` 过滤，字节码指令数与手写循环差异 < 5%

---

### Phase C：属性宏与元编程（预计 3-4 周）

**目标**：建立装饰器/属性宏基础设施，支撑数据类、属性、测试标记

| 周次 | 任务 | 涉及的文件 | 测试要求 |
|------|------|-----------|----------|
| C-1 | **属性语法 `#[attr]` / `#[attr(args)]`** | `src/lexer/token.h`（`#` 已存在？需确认），`src/parser/parser.h`, `src/ast/ast.h` | `src/test/test_attributes.cpp` |
| C-2 | **编译期属性处理器框架** | `src/frontend/attribute_processor.h` (新建) | 处理器注册/分发测试 |
| C-3 | **`#[derive(Clone, Debug, Eq)]` 宏** | `src/frontend/derive_macros.cpp` (新建) | `src/test/test_derive_macro.cpp` |
| C-3 | **`#[property(get, set)]` 宏** | `src/frontend/property_macro.cpp` (新建) | `src/test/test_property_macro.cpp` |
| C-4 | **`#[test]` 标记 + 测试运行器** | `src/frontend/test_macro.cpp`, `src/test/test_runner.cpp` | 自动收集 `#[test]` 函数 |

**里程碑 C**：能用 `#[derive(Clone, Debug)] struct Point { x: i64, y: i64 }` 自动生成对应方法

---

### Phase D：高级控制流与资源管理（预计 2-3 周）

**目标**：完善错误处理、资源管理、异步生态

| 周次 | 任务 | 涉及的文件 | 测试要求 |
|------|------|-----------|----------|
| D-1 | **`with` / 上下文管理器** | `src/parser/parser.h`, `src/ast/ast.h`, `src/optimizer/with_desugarer.cpp` (新建) | `src/test/test_with_stmt.cpp` |
| D-2 | **`raise from` 错误链** | `src/parser/parser.h`, `src/ast/ast.h`, `src/type/error_effect.cpp` | `src/test/test_raise_from.cpp` |
| D-3 | **`async for` / `async with`** | `src/parser/parser.h`, `src/bytecode/bytecode_compiler.cpp` | `src/test/test_async_iter.cpp` |
| D-4 | **生成器 `yield`（状态机编译）** | `src/parser/parser.h`, `src/ast/ast.h`, `src/optimizer/generator_desugarer.cpp` (新建), `src/bytecode/bytecode_compiler.cpp` | `src/test/test_generator.cpp` |

**里程碑 D**：`with open("file.txt") as f { print(f.read()); }` 正确编译执行

---

### Phase E：标准库填充（预计 2 周，可并行）

**目标**：补齐常用标准库函数，提升语言可用性

| 周次 | 任务 | 说明 |
|------|------|------|
| E-1 | **集合算法**：`all`, `any`, `sum`, `min`, `max`, `fold`, `reduce` | 纯库实现，无语法改动 |
| E-2 | **字符串方法**：`split`, `join`, `replace`, `trim`, `starts_with`, `ends_with` | 标准库 `string` 模块 |
| E-3 | **容器方法**：`Map::get`, `Set::insert`, `Array::sort` | 丰富内置类型 API |
| E-4 | **IO 基础**：`print`, `read_line`, `open`, `close` | 文件与标准 IO |

**里程碑 E**：能用 Claw 写一个简单的文本处理脚本（读取、过滤、输出）

---

## 五、总时间线

```
Phase A: 语法糖基础层    ████████████████████  2-3 周
Phase B: 推导式与集合    ████████████████████  2-3 周
Phase C: 属性宏与元编程  ████████████████████████  3-4 周
Phase D: 高级控制流      ████████████████████  2-3 周
Phase E: 标准库填充      ████████████          2 周  (可与 A-D 并行)
─────────────────────────────────────────────────────────
总计：约 11-15 周（单线程），8-10 周（Phase E 并行）
```

---

## 六、风险与回退策略

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 属性宏系统改动面大 | 高 | Phase C 拆分为语法解析（C-1）和处理器（C-2-C-4），C-1 可独立交付 |
| 生成器状态机编译复杂 | 高 | Phase D 中 `yield` 放到最后，若时间不足可延至下一计划 |
| 推导式脱糖与现有优化 Pass 冲突 | 中 | 在 IteratorDesugarer 之后、FunctionInliner 之前插入 ComprehensionDesugarer |
| `self` receiver 与现有函数调用解析冲突 | 中 | 优先尝试 `obj.method()` 解析，fallback 到普通函数调用 |

---

## 七、附录：编号索引

如需快速定位某个特性，在上表中搜索 `#编号`：
- `#1-11`：基础语法
- `#12-28`：数据类型与字面量
- `#29-36`：控制流
- `#37-49`：函数
- `#50-59`：类与 OOP
- `#60-66`：错误处理
- `#67-71`：模块与导入
- `#72-80`：高级特性
- `#81-90`：标准库模式
