---
type: project
created: 2026-06-02
status: active
---

# Claw Compiler 生产部署任务计划

## 当前状态快照 (2026-06-02)

- 版本: 0.2.0
- 源码: ~319文件 / ~90K行C++ / ~5K行测试
- 构建: Makefile完善，1个编译warning，依赖自动检测
- CI/CD: GitHub Actions (macOS+Linux)，文档自动部署
- 已生成二进制: claw, claw-lsp, claw-repl
- 缺失二进制: claw-debugger

## 生产就绪评分: ~72%

| 维度 | 评分 | 说明 |
|------|------|------|
| 语言核心 | 85% | 泛型、模式匹配、错误效应、迭代器、属性宏均已实现 |
| 编译器后端 | 80% | C/LLVM/AOT/Bytecode/JIT/WASM代码生成器齐全 |
| 优化管线 | 90% | 13+遍优化，支持-O0/-O1/-O2/-O3，迭代收敛 |
| 执行引擎 | 60% | AST解释器稳定；Bytecode/VM/JIT需稳定化 |
| 开发工具 | 55% | LSP/REPL已构建但未验证；调试器未构建 |
| 包管理器 | 80% | 完整的清单/解析/锁定/缓存实现 |
| 测试覆盖 | 65% | 大量单元测试通过，缺少集成测试和部分二进制 |
| 构建系统 | 85% | Makefile健壮，支持安装目标 |
| CI/CD | 60% | 有基础CI，缺少发布产物和自动发布流程 |
| 文档 | 55% | 设计文档丰富，缺少用户教程 |
| 代码质量 | 90% | 干净构建仅1个warning |

## 任务计划

### P0 - 稳定化（立即执行，1-2周）

1. 修复Bytecode VM主函数查找问题
   - 现象: 测试中出现 "No main function found in bytecode module"
   - 影响: Bytecode/VM执行模式不可用
   - 文件: src/bytecode/bytecode_compiler.cpp, src/vm/claw_vm.cpp

2. 补全缺失的测试二进制构建
   - 缺失: test_auto_scheduler, test_debugger, test_wasm_ir
   - 影响: 相关模块无自动化测试保护
   - 文件: Makefile, src/test/test_auto_scheduler.cpp 等

3. 构建并验证claw-debugger
   - 现象: debugger二进制未生成
   - 影响: 调试工具链缺失
   - 文件: Makefile, src/debugger/*.cpp

4. 修复AOT链接器warning
   - 现象: ld: no platform load command found in '*.o'
   - 影响: AOT产物在严格环境下可能无法运行
   - 文件: src/codegen/mach_o_writer.cpp 或相关

### P1 - 测试与质量（2周）

5. 添加端到端集成测试套件
   - 目标: 同一.claw源文件在--run/--mode=bytecode/--mode=jit/--aot下输出一致
   - 文件: test/integration_test_runner.cpp, tests/e2e/

6. 集成代码覆盖率报告(lcov)
   - 目标: make coverage成功生成HTML报告，CI中设置门槛
   - 文件: Makefile, .github/workflows/ci.yml

7. 验证LSP服务器和REPL基本功能
   - 目标: 确认claw-lsp能响应补全/诊断/跳转，claw-repl支持多行和变量存储
   - 文件: src/lsp/lsp_server.cpp, src/repl/claw_repl_integrated.cpp

8. 标准库VM运行时方法完善
   - 目标: string.length()、array.push/pop/len等方法在VM中可用
   - 文件: src/vm/claw_vm.cpp, src/stdlib/

### P2 - 开发者体验（2-3周）

9. 完善错误恢复与多错误报告
   - 目标: 编译器在第一个错误后继续努力解析，一次报告多个错误
   - 文件: src/parser/parser.h, src/type/type_checker.cpp

10. 包管理器CLI与主编译器连通
    - 目标: `claw install` / `claw build` 等命令可用，解析Claw.toml
    - 文件: src/main.cpp, src/package/package_manager.cpp

11. LSP VSCode扩展打包
    - 目标: 提供可安装的VSCode插件
    - 文件: editors/vscode/

### P3 - 发布准备（2周）

12. 完善CI/CD和发布流程
    - 目标: Release构建产物上传、多平台静态链接二进制、版本标签触发自动发布
    - 文件: .github/workflows/ci.yml, .github/workflows/release.yml

13. Homebrew formula和Docker镜像
    - 目标: `brew install claw` 和 `docker run claw` 可用
    - 文件: homebrew/, Dockerfile

14. 编写用户-facing文档
    - 目标: Getting Started、语言教程、标准库参考、示例集
    - 文件: docs/getting-started.md, docs/tutorial.md, docs/std/

15. 性能基准套件与回归监控
    - 目标: 建立fibonacci/sort/matrix等基准，CI中检测性能回归
    - 文件: benchmark/suite/, .github/workflows/ci.yml

## 风险与阻塞

- **最高风险**: Bytecode/VM执行链路不稳定，影响"一种源码，多种后端"的核心价值主张
- **阻塞点**: lcov未安装导致覆盖率报告不可用；debugger未编译导致无法调试VM问题
- **外部依赖**: LLVM版本兼容性、libmsquic可用性（已在CI中通过CLAW_ENABLE_WEBTRANSPORT=0缓解）

## 验收标准

- `make test` 全部通过，无 "No main function found" 错误
- `make coverage` 生成报告，行覆盖率 >= 60%
- `make all` 生成 claw + claw-lsp + claw-repl + claw-debugger
- AOT编译无linker warning
- 集成测试验证四种执行模式输出一致
- CI中发布流程可自动生成GitHub Release
