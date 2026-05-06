# Claw Compiler 生产就绪路线图

> 分析时间: 2026-04-29 | 代码量: 105K LoC | 64 个模块 | 编译状态: ✅ PASSING

## 当前能力矩阵

| 功能 | 状态 | 说明 |
|------|------|------|
| Lexer 词法分析 | ✅ 可用 | 支持完整关键字集 (40+ keywords) |
| Parser 语法分析 | ⚠️ 部分 | 递归下降, 缺 struct/class/import/closure |
| AST | ✅ 可用 | 1252 行定义, 序列化支持 |
| 类型检查 | ⚠️ 部分 | 基础类型推断, 缺泛型实例化/生命周期 |
| IR 生成 | ⚠️ 部分 | 基础指令集, 缺结构体/闭包/member access |
| IR 优化 | ✅ 可用 | 13 个 Pass (O0-O3), 含全局CSE/别名分析/IRVerifier |
| 解释器 | ✅ 可用 | AST 直接解释执行, 基础功能正常 |
| Bytecode/VM | ⚠️ 部分 | 编译/执行框架有, 但 VM 功能不完整 |
| JIT | ⚠️ 部分 | LLVM JIT 框架大, 但实际编译能力有限 |
| Native Codegen | ⚠️ 桩 | x86-64/ARM64/RISC-V emitter 存在但为 stub |
| C Codegen | ✅ 可用 | 可生成 C 代码 |
| 前端诊断 | ✅ 可用 | 错误码 + 源码定位 + 恢复 |
| 包管理 | ⚠️ 桩 | manifest/resolver/lock 文件结构有, 未连通 |
| LSP | ⚠️ 桩 | JSON-RPC 协议层有, 未实现 |
| Debugger | ⚠️ 桩 | 断点/单步框架有, 未连通 |
| 测试 | ⚠️ 极少 | 仅 lexer/parser 单元测试, 无集成测试 |

## 实测功能验证

| 测试用例 | 结果 | 说明 |
|----------|------|------|
| `fn add(a,b) + print` | ✅ 7 | 基础算术+函数调用正常 |
| `fib(10)` 递归 | ✅ 55 | 递归调用正常 |
| `for i in arr` 迭代 | ❌ | "Unsupported for loop iterable type" |
| 闭包/高阶函数 | ❌ | Parser 不支持匿名函数语法 |
| struct/record | ❌ | Parser 不支持 struct 定义 |
| class/method | ❌ | Parser 不支持 class 定义 |
| import 模块 | ❌ | Parser 不支持 import 语法 |
| match 模式匹配 | ⚠️ null | 语法解析但不执行匹配逻辑 |
| string.length() | ⚠️ null | 方法调用返回 null |
| 泛型函数 | ✅ 42 | 泛型实例化正常 |
| 错误恢复 | ⚠️ 部分 | 检测到错误但只报第一个就停止 |

---

## 生产就绪开发计划

### Phase 0: 基础设施 (2-3 周)
> 目标: 让项目可以被持续验证

**P0-1: 构建系统现代化**
- [ ] CMakeLists.txt 与 Makefile 同步验证
- [ ] CI/CD 流水线 (GitHub Actions)
  - Linux (GCC + Clang) / macOS / Windows (MSVC)
  - 自动 build + test
- [ ] 依赖管理: vcpkg/conan 集成 LLVM
- [ ] 构建变体: Debug/Release/Sanitizer (ASan/UBSan)

**P0-2: 测试框架**
- [ ] 统一测试框架 (gtest 或 catch2)
- [ ] 测试分类: unit / integration / e2e
- [ ] 最低测试覆盖:
  - Lexer: 100% (token 全覆盖)
  - Parser: 每个语法规则 ≥1 个正向 + 1 个负向测试
  - Interpreter: 每种表达式/语句类型 ≥1 个测试
  - IR Optimizer: 每个 Pass ≥1 个测试
- [ ] 测试覆盖率报告 (lcov/llvm-cov)
- [ ] 回归测试集 (已知 bug 修复验证)

**P0-3: 代码质量**
- [ ] 清理 182 个 compiler warning
- [ ] clang-tidy / clang-format 配置
- [ ] 文档生成 (Doxygen)
- [ ] Remove DEBUG prints (console output pollution)

### Phase 1: 语言核心完善 (4-6 周)
> 目标: 支持语言规范中的核心特性

**P1-1: Parser 补全**
- [ ] `struct` 定义与实例化
- [ ] `class` 定义与方法 (含 `self` 参数)
- [ ] `import` / `use` / `mod` 模块导入
- [ ] 匿名函数 / Lambda 闭包语法
- [ ] `match` 模式匹配执行逻辑
- [ ] `enum` 枚举类型定义
- [ ] `impl` trait 实现
- [ ] 错误恢复增强 (多错误继续解析)

**P1-2: 类型系统增强**
- [ ] struct/class 类型完整支持
- [ ] enum + variant 类型
- [ ] 泛型约束 (where clause / trait bound)
- [ ] 类型推断增强 ( Hindley-Milner 或简化版)
- [ ] 可空类型 / Optional
- [ ] 所有权检查 (至少标注阶段, 不强制)

**P1-3: 语义分析增强**
- [ ] 变量先定义后使用检查
- [ ] 类型兼容性严格检查 (当前大量 unknown)
- [ ] 函数签名完整性检查
- [ ] struct/class 成员访问合法性
- [ ] match 穷尽性检查

**P1-4: 标准库基础**
- [ ] `std.io`: print/println/readline
- [ ] `std.string`: length/concat/split/substring
- [ ] `std.array`: push/pop/len/map/filter
- [ ] `std.math`: sin/cos/sqrt/abs/min/max
- [ ] `std.collections`: HashMap/HashSet/Vec/Queue
- [ ] `std.fs`: read_file/write_file/exists

### Phase 2: 执行引擎完善 (3-4 周)
> 目标: 所有执行模式都能正确运行

**P2-1: 解释器增强**
- [ ] struct/class 实例化与方法调用
- [ ] 闭包捕获 (lexical scoping)
- [ ] for-in 迭代器协议
- [ ] match 模式执行
- [ ] 字符串方法调用
- [ ] 数组方法 (push/pop/len)

**P2-2: Bytecode/VM 完善**
- [ ] 补全所有 OpCode 实现
- [ ] 函数调用栈帧完善
- [ ] 闭包 + upvalue 支持
- [ ] struct/class 对象布局
- [ ] GC 或引用计数
- [ ] 性能基准测试 vs 解释器

**P2-3: JIT 最小可用**
- [ ] 基础表达式 JIT 编译 (算术/比较/逻辑)
- [ ] 函数调用 JIT
- [ ] JIT fallback 到解释器 (未支持特性)
- [ ] JIT 性能基准测试 vs 解释器 vs VM

### Phase 3: 工具链 (3-4 周)
> 目标: 开发者体验

**P3-1: 包管理器**
- [ ] claw.pkg 清单格式
- [ ] 依赖解析 (语义化版本)
- [ ] 注册表 (本地 + 远程)
- [ ] claw install / claw publish / claw update

**P3-2: LSP 服务器**
- [ ] textDocument/completion (自动补全)
- [ ] textDocument/hover (类型信息)
- [ ] textDocument/diagnostic (实时错误)
- [ ] textDocument/definition (跳转定义)
- [ ] textDocument/references (查找引用)
- [ ] VSCode 扩展

**P3-3: 调试器**
- [ ] 断点 (行/函数/条件)
- [ ] 单步执行 (step over/into/out)
- [ ] 变量检查 (局部/全局)
- [ ] 调用栈展示
- [ ] CLI 调试器界面

**P3-4: 格式化器**
- [ ] claw-fmt 自动格式化
- [ ] 配置文件 (.clawfmt)
- [ ] 与 clang-format 风格对齐

### Phase 4: 性能与优化 (2-3 周)
> 目标: 编译速度和运行时性能

**P4-1: 编译速度**
- [ ] 增量编译 (文件级)
- [ ] 解析缓存
- [ ] 并行编译 (多文件)
- [ ] 编译时间基准 + 回归监控

**P4-2: 运行时性能**
- [ ] IR 优化 Pass 有效性验证 (基准测试)
- [ ] JIT 编译热路径优化
- [ ] 内存分配优化 (对象池/arena)
- [ ] 性能基准套件 (fibonacci/sort/json-parse/regex)

**P4-3: Native Codegen 最小可用**
- [ ] x86-64 基础代码生成 (算术/调用/控制流)
- [ ] ELF/Mach-O 目标文件输出
- [ ] 链接器集成 (system linker)
- [ ] 交叉编译支持 (ARM64/RISC-V)

### Phase 5: 发布准备 (2 周)
> 目标: 1.0.0 发布

**P5-1: 文档**
- [ ] 语言参考手册 (从 spec 完善)
- [ ] 标准库文档
- [ ] 教程 (Getting Started / Examples / Cookbook)
- [ ] 架构文档 (内部设计, 贡献者指南)

**P5-2: 分发**
- [ ] 静态链接二进制 (Linux/macOS/Windows)
- [ ] Homebrew formula
- [ ] Docker image
- [ ] 安装器 (Windows/macOS .pkg)

**P5-3: 社区**
- [ ] GitHub Release 流程
- [ ] CONTRIBUTING.md
- [ ] Issue/PR 模板
- [ ] 代码行为准则
- [ ] 变更日志 (CHANGELOG.md)

---

## 优先级排序 (立即行动)

| 优先级 | 任务 | 预估工时 | 依赖 |
|--------|------|----------|------|
| 🔴 P0 | 清理 182 个 warning | 2天 | 无 |
| 🔴 P0 | 测试框架搭建 (gtest) | 3天 | 无 |
| 🔴 P0 | Lexer 单元测试全覆盖 | 2天 | P0 测试框架 |
| 🟠 P1 | Parser 补全 struct/class | 5天 | 无 |
| 🟠 P1 | 解释器 struct/class 支持 | 3天 | P1 Parser |
| 🟠 P1 | for-in 迭代器修复 | 1天 | 无 |
| 🟠 P1 | string 方法实现 | 2天 | 无 |
| 🟡 P2 | 闭包/Lambda 支持 | 5天 | P1 Parser |
| 🟡 P2 | 模块系统 (import) | 5天 | P1 Parser |
| 🟡 P2 | CI/CD 流水线 | 3天 | P0 测试 |
| 🟢 P3 | LSP 服务器 | 10天 | P1 完成 |
| 🟢 P3 | 包管理器 | 10天 | P1 完成 |

**总预估: Phase 0-5 合计 14-20 周 (1人全职)**

---

## 距离生产就绪的关键差距

### 🔴 Critical (不修复就不能用)
1. **Parser 缺 struct/class/import/closure** — 现代语言最基本的特性
2. **无测试覆盖** — 任何改动都可能静默破坏功能
3. **大量 DEBUG 输出** — 用户体验差

### 🟠 High (严重影响可用性)
4. **for-in 迭代器不工作** — 最常见的循环模式
5. **类型检查返回 unknown** — 类型系统形同虚设
6. **string/数组方法缺失** — 日常编程基本操作
7. **Bytecode/JIT 未连通** — 只有解释器能跑

### 🟡 Medium (影响专业用户)
8. **无 CI/CD** — 无法保证跨平台
9. **无 LSP** — IDE 支持=0
10. **182 个 warning** — 代码质量信号差

### 🟢 Low (锦上添花)
11. 包管理器、调试器、格式化器、Native Codegen
