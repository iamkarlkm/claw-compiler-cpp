# Claw 编译器项目结构

## 完整目录树

```
claw-compiler/
│
├── README.md                          # 项目主文档
├── UPDATE_SUMMARY.md                   # 本次更新总结
├── PROJECT_STRUCTURE.md                # 本文件
│
├── dev_status.md                      # 开发状态跟踪
├── feature_list.md                    # 功能清单
│
├── === 语言规范和设计文档 ===
│   ├── claw-language-spec.md          # 语言规范
│   ├── claw-type-system.md            # 类型系统
│   ├── claw-runtime-architecture.md   # 运行时架构
│   ├── core-architecture.md           # 核心架构
│   ├── claw-compiler-design.md        # 编译器设计
│   ├── claw-minimal-type-system.md    # 最小类型系统
│   ├── claw-naming-model.md          # 命名模型
│   ├── claw-syntax-update.md         # 语法更新
│   ├── claw-comment-syntax.md         # 注释语法
│   ├── claw-event-system.md          # 事件系统
│   └── self-modification-plan.md      # 自修改计划
│
├── === 新增：张量优化系统文档 (2025-04-11) ===
│   ├── claw-tensor-optimization.md    # 张量优化系统设计
│   ├── claw-ml-compiler-integration.md  # ML 编译器集成设计
│   ├── claw-tensor-quickstart.md      # 快速开始指南
│   └── claw-tensor-example.claw      # 示例代码
│
├── === 示例代码 ===
│   ├── calculator.claw                # 基础计算器
│   ├── claw-example.claw              # 完整示例
│   ├── claw-pubsub-example.claw       # 发布/订阅示例
│   ├── claw-simple-syntax.claw        # 简单语法
│   ├── claw-binary-naming.claw        # 二进制命名
│   └── claw-tensor-example.claw      # 张量示例（新增）
│
└── === 源代码 ===
    │
    ├── === 已完成模块 ===
    ├── main.cpp                       # 主入口
    ├── main_v2.cpp                   # V2 入口
    ├── main_debug.cpp                 # 调试入口
    │
    ├── common/                       # 公共模块
    │   └── common.h                 # 公共类型（~200 行）
    │
    ├── lexer/                        # 词法分析器
    │   ├── lexer.h                   # Lexer 实现（~500 行）
    │   └── token.h                   # Token 定义
    │
    ├── parser/                       # 语法分析器
    │   ├── parser.h                  # Parser 实现（~1000 行）
    │   └── parser.h.bak             # 备份文件
    │
    └── ast/                         # 抽象语法树
        └── ast.h                    # AST 定义（~500 行）
    │
    ├── === 待实现模块（张量优化系统）===
    ├── tensor/                       # 张量类型和 IR
    │   ├── tensor_type.h            # 张量类型推断器
    │   ├── tensor_ops.h             # 张量操作
    │   └── tensor_ir.h             # TensorIR 抽象
    │
    ├── scheduler/                    # 自动调度系统
    │   ├── schedule_space.h         # 搜索空间
    │   ├── search_strategy.h        # 搜索策略
    │   ├── cost_model.h            # 成本模型
    │   └── ml_optimizer.h          # ML 优化器
    │
    └── backend/                     # 目标后端
        ├── target.h                # 目标配置
        ├── codegen.h               # 代码生成
        └── runtime.h              # 运行时支持
    │
    ├── === 测试文件 ===
    ├── test_simple.cpp                # 简单测试
    ├── test_parser.cpp                # Parser 测试
    ├── test_debug.cpp                 # 调试测试
    ├── test_debug2.cpp
    ├── test_debug3.cpp
    ├── test_debug4.cpp
    ├── test_debug5.cpp
    ├── test_debug6.cpp
    ├── test_debug7.cpp
    ├── test_debug8.cpp
    └── test_debug9.cpp
```

---

## 模块依赖关系

### 当前实现的模块

```
main.cpp
  └── common.h
      ├── SourceLocation
      ├── SourceSpan
      ├── CompilerError
      └── DiagnosticReporter
  └── lexer.h
      ├── token.h
      └── common.h
  └── parser.h
      ├── lexer/token.h
      ├── ast/ast.h
      └── common.h
  └── ast/ast.h
      ├── lexer/token.h
      └── common.h
```

### 新增模块的依赖关系

```
main.cpp (扩展后)
  └── tensor/type_inferencer.h
      ├── tensor/tensor_ops.h
      ├── ast/ast.h
      └── common.h
  └── tensor/tensor_ir.h
      ├── tensor/type_inferencer.h
      ├── ast/ast.h
      └── common.h
  └── scheduler/auto_scheduler.h
      ├── scheduler/search_strategy.h
      ├── scheduler/cost_model.h
      ├── scheduler/schedule_space.h
      ├── tensor/tensor_ir.h
      └── common.h
  └── backend/target.h
      ├── tensor/tensor_ir.h
      └── common.h
  └── backend/jit_compiler.h
      ├── backend/target.h
      └── common.h
```

---

## 编译器流程图

### 当前流程

```
源代码 (.claw)
    │
    ▼
┌─────────────┐
│   Lexer     │ 词法分析
│  (~500 行)  │
└─────────────┘
    │
    ▼ (Tokens)
┌─────────────┐
│   Parser    │ 语法分析
│ (~1000 行)  │
└─────────────┘
    │
    ▼ (AST)
┌─────────────┐
│    AST      │ 抽象语法树
│  (~500 行)  │
└─────────────┘
    │
    ▼
[待实现] 语义分析
    │
    ▼
[待实现] 中间代码
    │
    ▼
[待实现] 代码生成
```

### 扩展后的流程（张量优化）

```
源代码 (.claw)
    │
    ▼
┌─────────────┐
│   Lexer     │ 词法分析
└─────────────┘
    │
    ▼
┌─────────────┐
│   Parser    │ 语法分析
└─────────────┘
    │
    ▼ (AST)
┌─────────────┐
│    AST      │ 抽象语法树
└─────────────┘
    │
    ▼
┌──────────────────────┐
│ 张量类型推断器      │ [新增]
│   type_inferencer   │
└──────────────────────┘
    │
    ▼
┌──────────────────────┐
│  TensorIR 生成器    │ [新增]
│   tensor_ir         │
└──────────────────────┘
    │
    ▼ (TensorIR Graph)
┌──────────────────────┐
│   自动调度系统       │ [新增]
│  auto_scheduler     │
│                      │
│  ├─ 搜索空间        │
│  ├─ 搜索策略        │
│  │   ├─ 随机       │
│  │   ├─ 进化       │
│  │   ├─ RL         │ [新增]
│  │   └─ LLM        │ [新增]
│  └─ 调度缓存       │
└──────────────────────┘
    │
    ▼
┌──────────────────────┐
│  ML 成本模型评估     │ [新增]
│   cost_model        │
│                      │
│  ├─ XGBoost         │ [新增]
│  ├─ Graph Transformer│ [新增]
│  └─ LLM             │ [新增]
└──────────────────────┘
    │
    ▼ (最优调度)
┌──────────────────────┐
│  目标后端代码生成    │ [新增]
│   backend           │
│                      │
│  ├─ CUDA 生成器     │ [新增]
│  ├─ LLVM IR 生成    │ [新增]
│  └─ TPU 生成器      │ [新增]
└──────────────────────┘
    │
    ▼ (机器码)
┌──────────────────────┐
│   JIT 编译器         │ [新增]
│   jit_compiler      │
│                      │
│  ├─ 编译缓存         │
│  └─ 运行时执行       │
└──────────────────────┘
    │
    ▼
优化机器码
```

---

## 模块大小对比

### 已完成模块
| 模块 | 代码行数 | 状态 |
|------|---------|------|
| Common | ~200 | ✅ 完成 |
| Lexer | ~500 | ✅ 完成 |
| Parser | ~1000 | ✅ 完成 |
| AST | ~500 | ✅ 完成 |
| **小计** | **~2200** | **90%** |

### 新增模块（待实现）
| 模块 | 预计代码行数 | 状态 |
|------|-------------|------|
| 张量类型系统 | ~800 | 🔄 设计完成 |
| TensorIR | ~1200 | 🔄 设计完成 |
| 自动调度系统 | ~1500 | 🔄 设计完成 |
| ML 成本模型 | ~1000 | 🔄 设计完成 |
| 目标后端 | ~1200 | 🔄 设计完成 |
| 性能测量框架 | ~400 | 🔄 设计完成 |
| **小计** | **~6100** | **设计完成** |

### 总计
| 类别 | 代码行数 |
|------|---------|
| 已完成 | ~2200 |
| 待实现 | ~6100 |
| **总计** | **~8300** |

---

## 开发阶段时间线

### Phase 1: 核心前端 ✅
**时间**: 2025-03-20 ~ 2025-04-10
**完成**:
- Lexer 实现
- Parser 实现
- AST 设计
- 公共工具

### Phase 2: 张量优化设计 ✅
**时间**: 2025-04-11
**完成**:
- 张量优化系统设计
- ML 编译器集成设计
- 快速开始指南
- 示例代码

### Phase 3: 基础设施 🔄
**时间**: 2025-04-12 ~ 2025-05-12 (4 周)
**计划**:
- 实现张量类型系统
- 实现 TensorIR 基础
- 集成到现有编译器

### Phase 4: 核心功能 📋
**时间**: 2025-05-13 ~ 2025-06-02 (3 周)
**计划**:
- 实现 TensorIR 生成器
- 实现调度原语
- 实现循环转换

### Phase 5: 自动调度 📋
**时间**: 2025-06-03 ~ 2025-06-30 (4 周)
**计划**:
- 实现搜索空间
- 实现随机/进化搜索
- 实现调度缓存

### Phase 6: ML 优化 📋
**时间**: 2025-07-01 ~ 2025-08-11 (6 周)
**计划**:
- 实现 XGBoost 成本模型
- 实现强化学习智能体
- 集成 LLM 接口

### Phase 7: 后端集成 📋
**时间**: 2025-08-12 ~ 2025-09-01 (3 周)
**计划**:
- 实现 CUDA 代码生成
- 实现 LLVM IR 生成
- 实现 JIT 编译器

---

## 关键技术栈

### 编译器核心
- **语言**: C++17
- **构建**: CMake (计划中)
- **代码生成**: LLVM IR、CUDA
- **解析**: 递归下降解析器

### 张量优化系统
- **自动调度**: Ansor 风格搜索
- **ML 模型**: XGBoost、PyTorch、TensorFlow
- **后端**: CUDA、ROCm、TPU
- **优化技术**:
  - 循环平铺（Tiling）
  - 向量化（Vectorization）
  - 循环展开（Unrolling）
  - 循环融合（Fusion）
  - 存储层次优化

### ML 训练工具
- **深度学习框架**: PyTorch 2.5+
- **图神经网络**: PyG、DGL
- **强化学习**: Stable Baselines3
- **LLM**: Transformers、Codellama

---

## 测试策略

### 单元测试
- [ ] 张量类型推断测试
- [ ] TensorIR 生成测试
- [ ] 调度原语测试
- [ ] ML 模型预测测试

### 集成测试
- [ ] 端到端编译测试
- [ ] 多后端代码生成测试
- [ ] JIT 编译和执行测试

### 性能测试
- [ ] 矩阵乘法基准测试
- [ ] 卷积操作基准测试
- [ ] 与 cuBLAS/cuDNN 对比
- [ ] 自动调优性能测试

### 回归测试
- [ ] 现有功能兼容性测试
- [ ] 性能回归检测

---

## 部署和分发

### 源码分发
```bash
# 从源码编译
git clone https://github.com/your-org/claw-compiler.git
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 预编译二进制
- Linux x86_64
- macOS arm64/x86_64
- Windows x86_64

### Docker 镜像
```dockerfile
FROM nvidia/cuda:11.8.0-devel-ubuntu22.04
# 安装依赖
# 构建 Claw 编译器
```

---

## 贡献者

- **核心团队**: OpenClaw (自动化开发)
- **设计文档**: AI 辅助设计 (2025-04-11)
- **参考资料**: TVM、PyTorch、TensorFlow 等开源项目

---

## 许可证

MIT License - 详见 LICENSE 文件

---

## 最后更新

2025-04-11

---

**Claw 编译器** - 为高性能计算和 AI 应用设计的下一代编程语言
