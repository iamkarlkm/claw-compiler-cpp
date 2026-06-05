---
title: Deployment Task Plan - Production Readiness
created: "2026-06-05"
type: task-plan
---

## Phase 1: CI/CD 修复 (1-2 周)

| # | Task | Priority | Est. Effort | Blocking |
|---|------|----------|-------------|----------|
| 1 | 修复 release.yml 中 claw-debugger 未构建的问题 | P0 | 0.5d | 所有 release |
| 2 | 补充 Windows CI 构建 (MSVC/MinGW) | P1 | 3d | 跨平台发布 |
| 3 | 修复 coverage 目标 (lcov 缺失 + CI 集成) | P1 | 1d | 质量度量 |
| 4 | 添加发布产物 SHA256 校验和与 GPG 签名 | P1 | 1d | 供应链安全 |

## Phase 2: 执行引擎稳定 (2-3 周)

| # | Task | Priority | Est. Effort | Blocking |
|---|------|----------|-------------|----------|
| 5 | 验证并修复 bytecode/VM 执行模式 | P0 | 3d | VM 可用性 |
| 6 | 验证 JIT 模式 | P1 | 2d | JIT 可用性 |
| 7 | 构建并验证 debugger 二进制 | P1 | 2d | 开发者工具 |

## Phase 3: 发布基础设施完善 (1-2 周)

| # | Task | Priority | Est. Effort | Blocking |
|---|------|----------|-------------|----------|
| 8 | 生成 deb/rpm 包 | P2 | 2d | Linux 分发 |
| 9 | Windows 安装器 (.msi 或 zip) | P2 | 2d | Windows 分发 |
| 10 | CHANGELOG 自动化生成 | P2 | 0.5d | 发布流程 |
| 11 | 版本兼容性测试 (不同 LLVM 版本) | P2 | 2d | 兼容性保证 |

## Phase 4: 文档与教程 (1-2 周)

| # | Task | Priority | Est. Effort | Blocking |
|---|------|----------|-------------|----------|
| 12 | Getting Started 教程 | P2 | 2d | 用户上手 |
| 13 | 标准库参考文档 | P2 | 3d | 开发者参考 |
| 14 | VSCode 扩展发布到市场 | P2 | 1d | IDE 支持 |

## 关键路径

修复 release workflow -> 补充 Windows CI -> 执行引擎稳定 -> 完整发布包

预估总工期: 5-9 周 (1人全职)
