# Claw 张量优化系统 - 快速开始指南

## 概述

本指南展示如何使用 Claw 语言和张量优化系统编写高性能张量计算代码。

---

## 基础张量操作

### 1. 创建张量

```claw
// 创建张量
let a: tensor<f32, [1024, 1024]> = tensor::zeros([1024, 1024]);
let b: tensor<f32, [1024, 1024]> = tensor::ones([1024, 1024]);
let c: tensor<f32, [1024, 1024]> = tensor::random([1024, 1024]);

// 动态维度张量
let x: tensor<f32, [dynamic, 256]> = tensor::zeros([batch_size, 256]);
```

### 2. 张量运算

```claw
// 基础算术
let sum = a + b;
let diff = a - b;
let product = a * b;
let quotient = a / b;

// 矩阵乘法
let matmul_result = a.matmul(b);

// 广播
let scalar: tensor<f32, [1, 1]> = tensor::fill([1, 1], 2.0);
let broadcasted = scalar.broadcast_to([1024, 1024]);

// 转置
let transposed = a.transpose([1, 0]);

// 形状变换
let reshaped = a.reshape([256, 4096]);
```

### 3. 卷积和池化

```claw
// 卷积操作
let input: tensor<f32, [batch, 64, 224, 224]> = ...;
let kernel: tensor<f32, [128, 64, 3, 3]> = ...;

let conv_out = tensor::conv2d(
    input, kernel,
    padding = "same",
    stride = [1, 1],
    dilation = [1, 1]
);

// 最大池化
let pool_out = tensor::max_pool2d(
    conv_out,
    kernel_size = [2, 2],
    stride = [2, 2],
    padding = "valid"
);

// 平均池化
let avg_pool = tensor::avg_pool2d(
    conv_out,
    kernel_size = [2, 2],
    stride = [2, 2]
);
```

---

## 自动优化

### 1. 使用 @auto_schedule 注解

```claw
// 自动调度 - 编译器自动搜索最优调度
@auto_schedule(
    search_space = {
        tile_sizes = [[8, 8], [16, 16], [32, 32]],
        vectorization = [8, 16, 32],
        unroll_factors = [2, 4, 8]
    },
    objective = "throughput",
    target = "cuda"
)
fn matmul_auto(A: tensor<f32, [M, K]>, B: tensor<f32, [K, N>])
    -> tensor<f32, [M, N]>
{
    return A.matmul(B);
}
```

### 2. 使用强化学习智能体

```claw
// RL 智能体优化
@rl_schedule(
    agent = "PPO",
    training_episodes = 10000,
    model_path = "models/matmul_agent.pt"
)
fn matmul_rl_optimized(A, B) {
    return A.matmul(B);
}
```

### 3. 使用 LLM 引导

```claw
// LLM 生成调度提示
@llm_schedule(
    model = "codellama-34b",
    prompt = "Generate optimal schedule for matrix multiplication on V100 GPU",
    temperature = 0.3
)
fn matmul_llm_optimized(A, B) {
    return A.matmul(B);
}
```

---

## 手动调度（高级）

对于需要细粒度控制的场景，可以使用 TensorIR 风格的调度原语：

```claw
compute matmul_kernel(A: tensor<f32, [M, K]>, B: tensor<f32, [K, N>]) {
    let C: tensor<f32, [M, N]>;

    for i in 0..M {
        for j in 0..N {
            for k in 0..K {
                C[i, j] += A[i, k] * B[k, j];
            }
        }
    }

    return C;
}

// 手动调度
schedule matmul_kernel {
    // 循环平铺
    tile(i, j, [16, 16]);

    // 向量化内层循环
    vectorize(i_inner);

    // 展开 k 循环
    unroll(k);

    // 并行化外层循环
    parallel(i_outer);

    // 绑定到 CUDA 线程
    bind(j_outer, "threadIdx.x");

    // 共享内存缓存
    cache_read(A, "shared");
    cache_read(B, "shared");
    cache_write(C, "local");
}
```

---

## 多后端支持

### 1. 为不同目标编译

```claw
// CUDA GPU 目标
@target(nvidia_v100)
fn matmul_gpu(A, B) {
    return A.matmul(B);
}

// CPU SIMD 目标
@target(cpu_x86_avx512)
fn matmul_cpu(A, B) {
    return A.matmul(B);
}

// TPU 目标
@target(tpu_v4)
fn matmul_tpu(A, B) {
    return A.matmul(B);
}
```

### 2. 运行时选择目标

```claw
fn main() {
    let A = tensor::random<f32, [1024, 1024]>();
    let B = tensor::random<f32, [1024, 1024]>();

    let C: tensor<f32, [1024, 1024]>;

    // 运行时选择最优目标
    if has_gpu() {
        C = matmul_gpu(A, B);
    } else {
        C = matmul_cpu(A, B);
    }
}
```

---

## 实际示例：CNN 层

```claw
// 完整的 CNN 卷积层
@auto_schedule(
    target = "cuda",
    objective = "throughput"
)
fn conv2d_layer(
    input: tensor<f32, [B, C, H, W]>,
    weight: tensor<f32, [K, C, R, S]>,
    bias: tensor<f32, [K]>,
    stride: [i32, i32],
    padding: [i32, i32]
) -> tensor<f32, [B, K, H', W']>
{
    // 卷积
    let conv_out = tensor::conv2d(
        input, weight,
        padding = padding,
        stride = stride
    );

    // 添加偏置
    let biased = conv_out + bias.broadcast_to([B, K, H', W']);

    // ReLU 激活
    let activated = tensor::relu(biased);

    // 批归一化
    let bn_out = tensor::batch_norm(activated);

    return bn_out;
}
```

---

## 完整示例：训练循环

```claw
fn forward_pass(
    input: tensor<f32, [B, 3, 224, 224]>,
    conv1_weight: tensor<f32, [64, 3, 7, 7]>,
    conv1_bias: tensor<f32, [64]>,
    fc_weight: tensor<f32, [1000, 512]>,
    fc_bias: tensor<f32, [1000]>
) -> tensor<f32, [B, 1000]>
{
    // Conv1
    let conv1 = tensor::conv2d(input, conv1_weight, padding = [3, 3], stride = [2, 2]);
    let conv1_bn = tensor::batch_norm(conv1 + conv1_bias);
    let relu1 = tensor::relu(conv1_bn);
    let pool1 = tensor::max_pool2d(relu1, [3, 3], [2, 2], padding = [1, 1]);

    // Flatten
    let flattened = pool1.reshape([B, -1]);

    // 全连接层
    let logits = flattened.matmul(fc_weight.transpose([1, 0])) + fc_bias;

    return logits;
}

fn compute_loss(
    logits: tensor<f32, [B, num_classes]>,
    labels: tensor<i32, [B]>
) -> tensor<f32, [1]>
{
    // Cross-entropy loss
    let loss = tensor::cross_entropy(logits, labels);
    return loss.reduce_mean();
}

fn train_step(
    model: Model,
    input: tensor<f32, [B, 3, 224, 224]>,
    labels: tensor<i32, [B]>,
    learning_rate: f32
)
{
    // 前向传播
    let logits = model.forward(input);
    let loss = compute_loss(logits, labels);

    // 反向传播
    let grads = tensor::compute_gradients(loss, model);

    // 优化器更新
    model.update_parameters(grads, learning_rate);

    return loss;
}
```

---

## 性能优化技巧

### 1. 使用自动调度

```claw
// ❌ 手动调度（耗时且容易出错）
schedule matmul_manual {
    tile(i, j, [16, 16]);
    vectorize(i_inner);
    unroll(k);
    parallel(i_outer);
    bind(j_outer, "threadIdx.x");
}

// ✅ 自动调度（简单且性能更好）
@auto_schedule(target = "cuda")
fn matmul_auto(A, B) {
    return A.matmul(B);
}
```

### 2. 利用张量融合

```claw
// ❌ 分离操作（多次内存访问）
let temp1 = a + b;
let temp2 = temp1 * c;
let result = temp2 / d;

// ✅ 融合操作（单次计算）
@fuse_ops
fn result = ((a + b) * c) / d;
```

### 3. 选择合适的搜索空间

```claw
// 对于小型张量，使用较小的搜索空间
@auto_schedule(
    search_space = {
        tile_sizes = [[4, 4], [8, 8]],
        vectorization = [8, 16]
    }
)
fn small_matmul(A: tensor<f32, [64, 64]>, B: tensor<f32, [64, 64]>) {
    return A.matmul(B);
}

// 对于大型张量，使用更大的搜索空间
@auto_schedule(
    search_space = {
        tile_sizes = [[8, 8], [16, 16], [32, 32], [64, 64]],
        vectorization = [8, 16, 32, 64],
        unroll_factors = [2, 4, 8, 16]
    }
)
fn large_matmul(A: tensor<f32, [4096, 4096]>, B: tensor<f32, [4096, 4096]>) {
    return A.matmul(B);
}
```

---

## 调试和分析

### 1. 查看生成的调度

```claw
// 使用 @print_schedule 注解查看调度
@auto_schedule(target = "cuda")
@print_schedule
fn matmul_debug(A, B) {
    return A.matmul(B);
}
```

输出：
```
Schedule for matmul_debug:
  tile(i, j, [16, 16])
  vectorize(i_inner)
  unroll(k)
  parallel(i_outer)
  bind(j_outer, "threadIdx.x")
  cache_read(A, "shared")
  cache_read(B, "shared")
  cache_write(C, "local")
```

### 2. 性能基准测试

```bash
# 编译并运行基准测试
clawc --benchmark matmul.claw

# 输出示例：
# matmul_1024x1024: 45.2 GFLOPS (vs cuBLAS: 48.5 GFLOPS, 93%)
# matmul_2048x2048: 48.7 GFLOPS (vs cuBLAS: 52.1 GFLOPS, 94%)
```

### 3. 可视化

```bash
# 生成 TensorIR 图可视化
clawc --visualize-ir conv2d.claw

# 生成调度可视化
clawc --visualize-schedule matmul.claw
```

---

## 与 PyTorch 集成

### 从 PyTorch 导出模型

```python
import torch

# 定义模型
class MyModel(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = torch.nn.Conv2d(3, 64, 7, 2, 3)
        self.fc = torch.nn.Linear(512, 1000)

    def forward(self, x):
        x = torch.relu(torch.nn.functional.max_pool2d(
            self.conv1(x), 3, 2, 1
        ))
        x = x.view(x.size(0), -1)
        x = self.fc(x)
        return x

# 导出为 Claw
model = MyModel()
torch.export_claw(model, "model.claw")
```

### 在 Claw 中使用导出的模型

```claw
// 导入的 PyTorch 模型
use "model.claw";

fn main() {
    let input = tensor::random<f32, [1, 3, 224, 224]>();

    // 使用导出的模型
    let output = MyModel::forward(input);

    println("Inference complete!");
}
```

---

## 常见问题

### Q: 如何选择合适的调度策略？

A:
- **小型张量** (< 256x256): 使用手动调度或小搜索空间
- **中型张量** (256-1024): 使用随机搜索
- **大型张量** (> 1024): 使用进化算法或 RL
- **关键路径**: 使用 LLM 引导搜索

### Q: 自动调优需要多长时间？

A:
- 首次调优: 5-10 分钟（取决于搜索空间大小）
- 后续调用: < 100ms（使用缓存）
- 可以使用 `--tune-time` 参数限制调优时间

### Q: 如何提高缓存的命中率？

A:
- 固定张量形状（避免动态维度）
- 使用相同的编译目标
- 定期清理旧缓存: `clawc --clean-cache`

### Q: 支持哪些硬件后端？

A:
- CPU: x86-64 (AVX2, AVX512), ARM (NEON)
- GPU: NVIDIA CUDA, AMD ROCm
- TPU: Google TPU v2/v4
- 其他: Vulkan, Metal（计划中）

---

## 下一步

1. 阅读完整设计文档:
   - [张量优化系统设计](./claw-tensor-optimization.md)
   - [ML 驱动编译器优化集成](./claw-ml-compiler-integration.md)

2. 尝试示例代码:
   ```bash
   cd examples/tensor
   ./run_examples.sh
   ```

3. 参与开发:
   - [贡献指南](./CONTRIBUTING.md)
   - [开发路线图](./dev_status.md)

---

## 参考资料

- [Apache TVM 官方文档](https://tvm.apache.org/docs/)
- [PyTorch 2.5 编译器文档](https://pytorch.org/docs/stable/torch.compiler.html)
- [Tensor Compiler Auto-Scheduling 论文](https://arxiv.org/abs/2501.00234)
- [机器学习编译器优化趋势](https://compileropt.ai/blog/2025-trends-ml-compiler-optimization/)
