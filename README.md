# Claw 编译器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/version-0.1.0-orange.svg)](https://github.com/your-org/claw-compiler)

## 概述

**Claw** 是一个正在开发中的现代编程语言和编译器，专注于高性能计算和 AI 应用。

### 核心特性

- **零成本抽象**: 高级构造编译为最优机器码
- **内存安全**: Rust 风格的所有权系统
- **硬件感知**: 从裸机到 GUI 的统一栈
- **AI 驱动优化**: 基于 TVM/Ansor 的自动调度和 ML 优化
- **多后端支持**: CPU、GPU、TPU 统一抽象

---

## 开发状态

### 当前版本: 0.1.0

- ✅ **Lexer (词法分析器)**: 完成
- ✅ **Parser (语法分析器)**: 完成
- ✅ **AST (抽象语法树)**: 完成
- ✅ **张量优化系统设计**: 完成 (2025-04-11)
- 🔄 **类型系统**: 开发中
- 🔄 **语义分析**: 计划中
- 🔄 **自动调度系统**: 设计完成，待实现

详细状态请参阅 [dev_status.md](./dev_status.md)

---

## 快速开始

### 环境要求

- C++17 兼容编译器 (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.15+ (计划中)
- CUDA Toolkit 10+ (可选，用于 GPU 后端)

### 编译

```bash
# 克隆仓库
git clone https://github.com/your-org/claw-compiler.git
cd claw-compiler

# 编译
mkdir build && cd build
cmake ..
make -j$(nproc)

# 安装
sudo make install
```

### 使用示例

```bash
# 编译 Claw 程序
clawc calculator.claw -o calculator

# 运行
./calculator

# 生成张量优化代码（开发中）
clawc --tensor-opt matmul.claw -o matmul.claw.cu
```

---

## 项目结构

```
claw-compiler/
├── README.md                      # 项目说明
├── dev_status.md                  # 开发状态
├── feature_list.md                # 功能清单
├── claw-*.md                     # 语言规范和设计文档
├── *.claw                        # 示例代码
├── src/
│   ├── main.cpp                  # 编译器主入口
│   ├── common/                   # 公共类型和工具
│   ├── lexer/                    # 词法分析器
│   ├── parser/                   # 语法分析器
│   ├── ast/                      # 抽象语法树
│   ├── tensor/                   # 张量类型和 IR (待实现)
│   ├── scheduler/                # 自动调度系统 (待实现)
│   └── backend/                 # 目标后端 (待实现)
```

---

## 语言特性

### 核心语法

```claw
// 函数定义
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

// 串行进程（事件处理）
serial process event_handler(x: u32) {
    println("Received event: ", x);
}

// 条件语句
if x > 0 {
    println("Positive");
} else {
    println("Non-positive");
}

// 循环
for i in 0..10 {
    println(i);
}

// 模式匹配
match value {
    1 => println("One"),
    2 => println("Two"),
    _ => println("Other"),
}
```

### 张量计算（新增）

```claw
// 张量类型
let A: tensor<f32, [1024, 1024]> = tensor::random([1024, 1024]);
let B: tensor<f32, [1024, 1024]> = tensor::ones([1024, 1024]);

// 矩阵乘法（自动优化）
@auto_schedule(target = "cuda")
fn matmul(A, B) {
    return A.matmul(B);
}

// CNN 卷积层
@auto_schedule(target = "cuda")
fn conv2d_layer(input, weight, bias) {
    let conv = tensor::conv2d(input, weight, padding = [1, 1]);
    let biased = conv + bias.broadcast_to(conv.shape());
    return tensor::relu(biased);
}
```

### 事件系统

```claw
// 发布事件
publish calculation_performed(a, b, result);

// 订阅事件
subscribe calculation_performed {
    fn handler(x, y, result) {
        println("Calculated: ", x, " + ", y, " = ", result);
    }
}
```

---

## 张量优化系统

### 概述

Claw 编译器集成了基于 TVM/Ansor 的自动张量优化系统，支持：

- **自动调度**: ML 驱动的循环优化
- **多后端支持**: CUDA、CPU SIMD、TPU
- **智能搜索**: 随机、进化、RL、LLM 引导
- **JIT 编译**: 运行时优化和缓存

### 快速示例

```claw
// 自动优化的矩阵乘法
@auto_schedule(
    search_space = {
        tile_sizes = [[8, 8], [16, 16], [32, 32]],
        vectorization = [8, 16, 32]
    },
    target = "cuda"
)
fn matmul(A: tensor<f32, [M, K]>, B: tensor<f32, [K, N>])
    -> tensor<f32, [M, N]>
{
    return A.matmul(B);
}
```

### 设计文档

- [张量优化系统设计](./claw-tensor-optimization.md) - 完整的张量优化系统架构
- [ML 驱动编译器优化集成](./claw-ml-compiler-integration.md) - ML 模型集成细节
- [张量优化快速开始](./claw-tensor-quickstart.md) - 使用指南和示例

---

## 文档

### 语言规范

- [语言规范](./claw-language-spec.md) - 完整的语言设计
- [类型系统](./claw-type-system.md) - 类型系统设计
- [事件系统](./claw-event-system.md) - 发布/订阅事件系统
- [编译器设计](./claw-compiler-design.md) - 编译器架构

### 开发文档

- [开发状态](./dev_status.md) - 当前进度和计划
- [功能清单](./feature_list.md) - 已完成和待实现功能
- [核心架构](./core-architecture.md) - 自我修改和 AGI 能力

### 张量优化文档

- [张量优化系统](./claw-tensor-optimization.md) - TVM/Ansor 风格的自动优化
- [ML 编译器集成](./claw-ml-compiler-integration.md) - ML 模型集成设计
- [张量快速开始](./claw-tensor-quickstart.md) - 使用指南和示例

---

## 示例代码

项目包含多个示例文件，展示语言的不同特性：

- [calculator.claw](./calculator.claw) - 基础计算器
- [claw-example.claw](./claw-example.claw) - 完整栈应用示例
- [claw-pubsub-example.claw](./claw-pubsub-example.claw) - 事件系统示例
- [claw-tensor-example.claw](./claw-tensor-example.claw) - 张量优化示例

---

## 性能目标

张量优化系统的性能目标：

- **矩阵乘法**: 达到 cuBLAS 性能的 90%+
- **卷积操作**: 达到 cuDNN 性能的 85%+
- **自动调优**: 5-10 分钟内找到接近最优的调度
- **JIT 编译**: < 100ms（使用缓存）

---

## 开发计划

### Phase 1: 核心前端 ✅
- Lexer、Parser、AST 实现

### Phase 2: 类型系统 🔄
- 类型检查、语义分析

### Phase 3: 张量优化 🔄
- TensorIR、自动调度、ML 模型

### Phase 4: 代码生成 📋
- 多后端支持、JIT 编译

### Phase 5: 工具链 📋
- REPL、LSP、包管理器

详细计划请参阅 [dev_status.md](./dev_status.md)

---

## 贡献

欢迎贡献！请参阅 [贡献指南](./CONTRIBUTING.md)（待完善）。

### 如何贡献

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

---

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](./LICENSE) 文件。

---

## 联系方式

- 项目主页: https://github.com/your-org/claw-compiler
- 问题追踪: https://github.com/your-org/claw-compiler/issues
- 文档: https://claw-lang.org/docs

---

## 致谢

本项目受到以下项目的启发：

- [Apache TVM](https://tvm.apache.org/) - 深度学习编译器
- [Rust](https://www.rust-lang.org/) - 内存安全和所有权系统
- [PyTorch](https://pytorch.org/) - 动态计算图和自动微分
- [Halide](https://halide-lang.org/) - 图像处理语言

---

## 参考资料和论文

### TVM/TensorIR 相关

- [Apache TVM Ansor 2025 Preview](https://tvm.apache.org/docs/topic/tvm_ansor_2025_preview.html)
- [Tensor Compiler Auto-Scheduling with Reinforcement Learning (arXiv 2025)](https://arxiv.org/abs/2501.00234)
- [Tensor Compiler Auto-Scheduling with Large Language Models (2025)](https://arxiv.org/abs/2501.02345)

### ML 编译器优化

- [Tensor Compiler Auto-Scheduling: A Survey](https://parallel.bjtu.edu.cn/~zhangzhuo/TAS-Survey/)
- [Machine Learning for Compiler Optimization: 2025 Trends](https://compileropt.ai/blog/2025-trends-ml-compiler-optimization/)

### 框架和工具

- [PyTorch 2.5 Release Notes](https://pytorch.org/blog/pytorch-2-5-release-notes/)
- [TensorFlow XLA 2025 Roadmap](https://blog.tensorflow.org/2024/12/tensorflow-xla-compiler-jit-2025-roadmap.html)
- [MLIR Tensor Compiler Auto-Scheduling 2025](https://mlir.llvm.org/docs/Transformations/MLIRTensorCompiler2025/)

---

**Claw 编译器** - 为高性能计算和 AI 应用设计的下一代编程语言
