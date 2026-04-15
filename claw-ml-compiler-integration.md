# Claw 编译器 ML 驱动优化集成设计

## 概述

本文档描述如何将 TVM/Ansor 风格的自动调度和 ML 优化集成到 Claw 编译器的现有架构中，实现从零开始构建 ML 驱动的编译器优化系统。

---

## 现有架构扩展

### 当前编译器流程

```
源代码 (.claw)
    ↓
Lexer (词法分析)
    ↓
Parser (语法分析)
    ↓
AST (抽象语法树)
    ↓
[待实现] 语义分析
    ↓
[待实现] 中间代码生成
    ↓
[待实现] 代码优化
    ↓
[待实现] 目标代码生成
```

### 扩展后的编译器流程

```
源代码 (.claw)
    ↓
Lexer (词法分析)
    ↓
Parser (语法分析)
    ↓
AST (抽象语法树)
    ↓
[新增] 张量类型检查
    ↓
[新增] TensorIR 生成
    ↓
[新增] 自动调度 (Ansor 风格)
    ↓
[新增] ML 成本模型评估
    ↓
[新增] 最优调度选择
    ↓
[新增] 目标后端代码生成 (CUDA/LLVM)
    ↓
优化机器码
```

---

## 新增模块设计

### 1. 张量类型推断器（Tensor Type Inference）

**文件**: `src/tensor/type_inferencer.h`

```cpp
namespace claw {
namespace tensor {

class TensorTypeInferencer {
public:
    struct TensorType {
        Type element_type;           // 元素类型 (f32, i32, etc.)
        std::vector<Expr> dims;     // 维度表达式
        bool is_dynamic = false;     // 是否有动态维度
        SourceSpan span;

        std::string to_string() const;
        bool is_compatible(const TensorType& other) const;
        size_t rank() const { return dims.size(); }
    };

    // 推断表达式的张量类型
    Result<TensorType> infer_expression(Expression* expr);

    // 推断函数调用的输出张量类型
    Result<TensorType> infer_call(CallExpr* call);

    // 检查张量操作的有效性
    Result<bool> check_binary_op(BinaryExpr* binary,
                                const TensorType& left,
                                const TensorType& right);

    // 广播规则检查
    Result<TensorType> apply_broadcast(
        const TensorType& left,
        const TensorType& right
    );

    // 形状推断
    Result<std::vector<Expr>> infer_shape(Operation op);

private:
    DiagnosticReporter* reporter;
    std::unordered_map<Expr*, TensorType> type_cache_;
};

} // namespace tensor
} // namespace claw
```

### 2. TensorIR 生成器

**文件**: `src/tensor/tensor_ir.h`

```cpp
namespace claw {
namespace tensor {

// TensorIR 节点类型
enum class TensorIROp {
    // 基础运算
    Add, Sub, Mul, Div, Mod, Pow,
    Min, Max, Abs, Sqrt, Log, Exp,

    // 线性代数
    MatMul, MatVec, VecMat,

    // 卷积和池化
    Conv2D, DepthwiseConv2D,
    MaxPool2D, AvgPool2D,

    // 变换
    Transpose, Reshape, Broadcast,
    Slice, Pad, Concat,

    // 归约
    ReduceSum, ReduceMean,
    ReduceMax, ReduceMin,

    // 自定义算子
    Custom
};

// TensorIR 节点
class TensorIRNode {
public:
    TensorIROp op;
    std::vector<TensorIRNode*> inputs;
    TensorType output_type;
    SourceSpan span;

    // 调度提示
    struct ScheduleHint {
        bool can_fuse = true;
        bool can_vectorize = true;
        bool can_parallel = true;
        int preferred_tile_size = 32;
    };

    ScheduleHint schedule_hint;

    virtual std::string to_string() const;
    virtual bool is_reduction() const;
    virtual bool is_parallelizable() const;

    // 获取计算复杂度
    int64_t compute_complexity() const;
    int64_t memory_traffic() const;
};

// TensorIR 图
class TensorIRGraph {
public:
    // 添加节点
    TensorIRNode* add_node(std::unique_ptr<TensorIRNode> node);

    // 连接节点
    void add_edge(TensorIRNode* src, TensorIRNode* dst);

    // 拓扑排序
    std::vector<TensorIRNode*> topological_sort() const;

    // 依赖分析
    std::vector<TensorIRNode*> get_dependencies(TensorIRNode* node) const;

    // 图优化
    void fuse_operations();
    void eliminate_dead_code();
    void constant_fold();

    // 保存/加载
    void save_to_file(const std::string& path) const;
    static TensorIRGraph load_from_file(const std::string& path);

private:
    std::vector<std::unique_ptr<TensorIRNode>> nodes_;
    std::vector<std::pair<TensorIRNode*, TensorIRNode*>> edges_;
    std::unordered_map<std::string, TensorIRNode*> name_map_;
};

// TensorIR 生成器
class TensorIRGenerator {
public:
    TensorIRGenerator(DiagnosticReporter* reporter);

    // 从 AST 生成 TensorIR
    Result<TensorIRGraph> generate(
        const ast::Program* program
    );

    // 生成单个表达式的 TensorIR
    Result<TensorIRNode*> generate_expression(
        const ast::Expression* expr
    );

    // 设置目标平台
    void set_target(const Target& target) { target_ = target; }

private:
    DiagnosticReporter* reporter_;
    Target target_;
    TensorTypeInferencer type_inferencer_;
    std::unordered_map<const ast::ASTNode*, TensorIRNode*> node_map_;
};

} // namespace tensor
} // namespace claw
```

### 3. 自动调度系统（Claw Ansor）

**文件**: `src/scheduler/auto_scheduler.h`

```cpp
namespace claw {
namespace scheduler {

// 调度原语
enum class SchedulePrimitive {
    Tile,           // 循环平铺
    Fuse,           // 循环融合
    Split,          // 循环分裂
    Vectorize,      // 向量化
    Unroll,         // 展开循环
    Parallel,       // 并行化
    BindThread,     // 绑定到线程
    CacheRead,      // 缓存读取
    CacheWrite,     // 缓存写入
    Reorder,        // 循环重排序
    Prefetch        // 预取
};

// 调度动作
struct ScheduleAction {
    SchedulePrimitive primitive;
    std::vector<int> parameters;  // 动作参数
    std::vector<size_t> loop_indices;  // 目标循环

    std::string to_string() const;
};

// 调度方案
class Schedule {
public:
    Schedule(const TensorIRGraph* graph);

    // 添加调度动作
    void add_action(const ScheduleAction& action);

    // 应用调度到 TensorIR
    void apply(TensorIRGraph* graph) const;

    // 预估性能
    float estimate_cost(const MLModel& model) const;

    // 序列化
    std::string serialize() const;
    static Schedule deserialize(const std::string& data);

    // 调度哈希（用于缓存）
    size_t hash() const;

private:
    const TensorIRGraph* graph_;
    std::vector<ScheduleAction> actions_;
};

// 搜索空间
class ScheduleSpace {
public:
    // 定义搜索空间
    void define_loop_tile_sizes(const std::vector<std::vector<int>>& sizes);
    void define_unroll_factors(const std::vector<int>& factors);
    void define_vector_sizes(const std::vector<int>& sizes);
    void define_parallel_strategies(const std::vector<std::string>& strategies);

    // 生成随机调度
    Schedule generate_random_schedule(const TensorIRGraph* graph) const;

    // 生成邻居调度（用于局部搜索）
    std::vector<Schedule> get_neighbors(const Schedule& schedule) const;

    // 搜索空间大小
    size_t estimate_size() const;

private:
    std::vector<std::vector<int>> tile_sizes_;
    std::vector<int> unroll_factors_;
    std::vector<int> vector_sizes_;
    std::vector<std::string> parallel_strategies_;
};

// 搜索策略基类
class SearchStrategy {
public:
    virtual ~SearchStrategy() = default;

    // 搜索最优调度
    virtual Schedule search(
        const TensorIRGraph* graph,
        const ScheduleSpace& space,
        const MLModel& cost_model,
        int max_trials
    ) = 0;

protected:
    // 评估调度性能（实际运行）
    float evaluate_schedule(const Schedule& schedule);
};

// 随机搜索
class RandomSearch : public SearchStrategy {
public:
    Schedule search(const TensorIRGraph* graph,
                  const ScheduleSpace& space,
                  const MLModel& cost_model,
                  int max_trials) override;
};

// 进化算法搜索
class EvolutionarySearch : public SearchStrategy {
public:
    EvolutionarySearch(int population_size = 20,
                     float mutation_rate = 0.1,
                     float crossover_rate = 0.8);

    Schedule search(const TensorIRGraph* graph,
                  const ScheduleSpace& space,
                  const MLModel& cost_model,
                  int max_trials) override;

private:
    int population_size_;
    float mutation_rate_;
    float crossover_rate_;

    Schedule crossover(const Schedule& a, const Schedule& b);
    Schedule mutate(const Schedule& schedule, const ScheduleSpace& space);
};

// 强化学习搜索
class ReinforcementLearningSearch : public SearchStrategy {
public:
    ReinforcementLearningSearch(const std::string& model_path);

    Schedule search(const TensorIRGraph* graph,
                  const ScheduleSpace& space,
                  const MLModel& cost_model,
                  int max_trials) override;

    // 训练智能体
    void train(const std::vector<ScheduleExperience>& experiences);

private:
    class RLAgent* agent_;
};

// 自动调度器
class AutoScheduler {
public:
    AutoScheduler(const std::string& strategy_name);

    // 设置搜索策略
    void set_strategy(std::unique_ptr<SearchStrategy> strategy);

    // 设置目标平台
    void set_target(const Target& target);

    // 调度张量程序
    Result<Schedule> schedule(
        const TensorIRGraph* graph,
        int max_trials = 1000
    );

    // 从缓存加载调度
    Result<Schedule> load_from_cache(const TensorIRGraph* graph);

    // 保存调度到缓存
    void save_to_cache(const TensorIRGraph* graph, const Schedule& schedule);

private:
    std::unique_ptr<SearchStrategy> strategy_;
    Target target_;
    ScheduleSpace search_space_;
    std::string cache_dir_ = ".claw_schedule_cache";
};

} // namespace scheduler
} // namespace claw
```

### 4. ML 成本模型

**文件**: `src/scheduler/ml_model.h`

```cpp
namespace claw {
namespace ml {

// 成本模型类型
enum class ModelType {
    XGBoost,           // XGBoost 回归
    RandomForest,      // 随机森林
    NeuralNetwork,     // 神经网络
    GraphTransformer,  // 图 Transformer（类似 Graphormer）
    LLM                // 大语言模型
};

// 特征提取器
class FeatureExtractor {
public:
    // 提取调度特征
    std::vector<float> extract_schedule_features(
        const TensorIRGraph& graph,
        const Schedule& schedule
    );

    // 提取图结构特征
    std::vector<float> extract_graph_features(
        const TensorIRGraph& graph
    );

    // 提取循环嵌套特征
    std::vector<float> extract_loop_features(
        const Schedule& schedule
    );

    // 提取内存访问特征
    std::vector<float> extract_memory_features(
        const TensorIRGraph& graph
    );

private:
    // 计算特征
    float compute_arithmetic_intensity(const TensorIRNode* node);
    float compute_data_reuse(const Schedule& schedule);
    float compute_parallel_efficiency(const Schedule& schedule);
};

// 成本模型
class CostModel {
public:
    virtual ~CostModel() = default;

    // 预测性能（单位：GFLOPS 或 ms）
    virtual float predict_cost(
        const TensorIRGraph& graph,
        const Schedule& schedule
    ) = 0;

    // 训练模型
    virtual void train(
        const std::vector<Schedule>& schedules,
        const std::vector<float>& actual_costs
    ) = 0;

    // 保存/加载模型
    virtual void save(const std::string& path) const = 0;
    virtual void load(const std::string& path) = 0;

    // 模型类型
    virtual ModelType get_type() const = 0;
};

// XGBoost 成本模型
class XGBoostModel : public CostModel {
public:
    XGBoostModel();

    float predict_cost(const TensorIRGraph& graph,
                      const Schedule& schedule) override;

    void train(const std::vector<Schedule>& schedules,
              const std::vector<float>& actual_costs) override;

    void save(const std::string& path) const override;
    void load(const std::string& path) override;

    ModelType get_type() const override { return ModelType::XGBoost; }

private:
    void* xgboost_handle_;  // XGBoost Booster handle
    FeatureExtractor feature_extractor_;
};

// 图 Transformer 模型（用于张量程序）
class GraphTransformerModel : public CostModel {
public:
    GraphTransformerModel(int hidden_dim = 256, int num_layers = 6);

    float predict_cost(const TensorIRGraph& graph,
                      const Schedule& schedule) override;

    void train(const std::vector<Schedule>& schedules,
              const std::vector<float>& actual_costs) override;

    void save(const std::string& path) const override;
    void load(const std::string& path) override;

    ModelType get_type() const override { return ModelType::GraphTransformer; }

private:
    int hidden_dim_;
    int num_layers_;
    FeatureExtractor feature_extractor_;
    // PyTorch/TensorFlow 模型接口
};

// LLM 辅助成本模型
class LLMCostModel : public CostModel {
public:
    LLMCostModel(const std::string& model_name = "codellama-34b");

    float predict_cost(const TensorIRGraph& graph,
                      const Schedule& schedule) override;

    // 使用 LLM 生成调度建议
    Schedule suggest_schedule(const TensorIRGraph& graph,
                            const ScheduleSpace& space);

    ModelType get_type() const override { return ModelType::LLM; }

private:
    std::string model_name_;
    FeatureExtractor feature_extractor_;
};

} // namespace ml
} // namespace claw
```

### 5. 目标后端

**文件**: `src/backend/target.h`

```cpp
namespace claw {
namespace backend {

// 目标平台类型
enum class TargetType {
    CPU_X86,
    CPU_ARM,
    CUDA,
    ROCm,
    TPU,
    Vulkan
};

// 目标配置
class Target {
public:
    Target(TargetType type, std::string name);

    // 配置选项
    void set_arch(const std::string& arch);
    void set_features(const std::vector<std::string>& features);
    void set_num_cores(int cores);
    void set_memory_bandwidth(const std::string& bandwidth);

    // 常用目标预设
    static Target cpu_x86_avx512();
    static Target cpu_arm_neon();
    static Target nvidia_v100();
    static Target nvidia_a100();
    static Target tpu_v4();

    // 目标信息
    TargetType get_type() const { return type_; }
    std::string get_name() const { return name_; }
    std::vector<std::string> get_features() const { return features_; }

    // 生成目标代码
    std::string generate_code(const TensorIRGraph& graph,
                           const Schedule& schedule);

private:
    TargetType type_;
    std::string name_;
    std::string arch_;
    std::vector<std::string> features_;
    int num_cores_;
    std::string memory_bandwidth_;
};

// CUDA 代码生成器
class CUDACodeGenerator {
public:
    std::string generate_kernel(const TensorIRGraph& graph,
                             const Schedule& schedule);

    std::string generate_host_wrapper(const TensorIRGraph& graph);

private:
    std::string generate_block_indices();
    std::string generate_thread_indices();
    std::string generate_shared_memory();
};

// LLVM IR 代码生成器
class LLVMIRGenerator {
public:
    LLVMIRGenerator();

    std::string generate(const TensorIRGraph& graph,
                       const Schedule& schedule);

    void optimize_ir();

    std::string assemble_to_machine_code();

private:
    void* llvm_context_;
    void* llvm_module_;
};

// JIT 编译器
class JITCompiler {
public:
    JITCompiler(const Target& target);

    // 编译 TensorIR 图
    void* compile(const TensorIRGraph& graph,
                  const Schedule& schedule);

    // 保存编译结果到缓存
    void save_to_cache(const TensorIRGraph& graph,
                      void* compiled_code);

    // 从缓存加载编译结果
    Result<void*> load_from_cache(const TensorIRGraph& graph);

    // 执行编译后的函数
    template<typename... Args>
    void execute(void* function, Args... args);

private:
    Target target_;
    std::string cache_dir_ = ".claw_jit_cache";
};

} // namespace backend
} // namespace claw
```

---

## 集成到主编译器

### 扩展 main.cpp

```cpp
// Claw Compiler Main Entry Point with Tensor Optimization

#include "tensor/type_inferencer.h"
#include "tensor/tensor_ir.h"
#include "scheduler/auto_scheduler.h"
#include "backend/target.h"
#include "backend/jit_compiler.h"

int main(int argc, char* argv[]) {
    // ... 现有的 lexer/parser 代码 ...

    // 1. 张量类型检查
    std::cout << "--- Tensor Type Inference ---\n";
    claw::tensor::TensorTypeInferencer type_inferencer;
    type_inferencer.set_reporter(&reporter);
    // 推断所有张量表达式的类型
    // ...

    // 2. 生成 TensorIR
    std::cout << "--- TensorIR Generation ---\n";
    claw::tensor::TensorIRGenerator ir_generator(&reporter);
    claw::backend::Target target = claw::backend::Target::nvidia_v100();
    ir_generator.set_target(target);

    auto tensor_ir = ir_generator.generate(program.get());
    if (!tensor_ir.ok()) {
        std::cerr << "TensorIR generation failed: "
                  << tensor_ir.unwrap_err() << "\n";
        return 1;
    }

    // 3. 自动调度
    std::cout << "--- Auto Scheduling ---\n";
    claw::scheduler::AutoScheduler scheduler("evolutionary");
    scheduler.set_target(target);

    // 尝试从缓存加载调度
    auto schedule_result = scheduler.load_from_cache(tensor_ir.unwrap().get());
    if (!schedule_result.ok()) {
        // 缓存未命中，执行自动搜索
        std::cout << "Searching for optimal schedule...\n";
        schedule_result = scheduler.schedule(tensor_ir.unwrap().get(), 1000);

        // 保存到缓存
        scheduler.save_to_cache(tensor_ir.unwrap().get(), schedule_result.unwrap());
    }

    auto schedule = schedule_result.unwrap();
    std::cout << "Schedule found!\n";

    // 4. 代码生成
    std::cout << "--- Code Generation ---\n";
    std::string kernel_code = target.generate_code(
        tensor_ir.unwrap().get(),
        schedule
    );

    // 保存生成的代码
    std::ofstream out_file("output.claw.cu");
    out_file << kernel_code;
    out_file.close();

    // 5. JIT 编译和执行（可选）
    claw::backend::JITCompiler jit(target);
    void* compiled_function = jit.compile(tensor_ir.unwrap().get(), schedule);

    std::cout << "--- Compilation Complete ---\n";
    std::cout << "Generated code saved to output.claw.cu\n";

    return 0;
}
```

---

## 依赖项管理

### 新增外部依赖

```cmake
# CMakeLists.txt

# 机器学习库
find_package(XGBoost REQUIRED)
find_package(ONNXRuntime)  # 用于图神经网络推理

# 图神经网络（可选，用于高级成本模型）
# find_package(PyTorch)  # 如果用 C++ frontend

# CUDA（如果需要 CUDA 后端）
find_package(CUDA)
if(CUDA_FOUND)
    target_link_libraries(claw_compiler ${CUDA_LIBRARIES})
endif()

# LLVM（用于 CPU 后端）
find_package(LLVM REQUIRED)
target_link_libraries(claw_compiler ${LLVM_LIBS})
```

### Python 依赖（用于训练 ML 模型）

```python
# requirements.txt

# 图神经网络
torch>=2.5.0
torch-geometric>=2.5.0
dgl>=1.1.0

# 传统 ML
xgboost>=2.0.0
scikit-learn>=1.5.0

# LLM 接口
transformers>=4.40.0
accelerate>=0.30.0

# 可视化和分析
tensorboard>=2.16.0
matplotlib>=3.9.0

# 数据处理
numpy>=1.26.0
pandas>=2.2.0
```

---

## 性能测量框架

### 基准测试工具

**文件**: `src/benchmark/benchmark.h`

```cpp
namespace claw {
namespace benchmark {

class Benchmark {
public:
    // 测量单个调度
    static float measure_schedule(
        const Schedule& schedule,
        const TensorIRGraph& graph,
        int warmup = 10,
        int iterations = 100
    );

    // 测量多个调度
    static std::vector<float> measure_schedules(
        const std::vector<Schedule>& schedules,
        const TensorIRGraph& graph
    );

    // 生成性能报告
    static void generate_report(
        const std::string& output_path,
        const std::vector<Schedule>& schedules,
        const std::vector<float>& times
    );

    // 与基准比较
    static void compare_with_baseline(
        const Schedule& schedule,
        const Schedule& baseline,
        const TensorIRGraph& graph
    );
};

} // namespace benchmark
} // namespace claw
```

---

## 测试策略

### 单元测试

```cpp
// tests/tensor/test_type_inference.cpp

TEST(TensorTypeInference, MatrixMultiplication) {
    // A: (1024, 1024) * B: (1024, 1024) -> C: (1024, 1024)
    TensorType A(Type::F32, {1024, 1024});
    TensorType B(Type::F32, {1024, 1024});

    TensorTypeInferencer inferencer;
    auto result = inferencer.check_binary_op(Op::MatMul, A, B);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.unwrap().dims, std::vector<size_t>{1024, 1024});
}

TEST(AutoScheduler, CacheHit) {
    // 测试调度缓存机制
    AutoScheduler scheduler("random");
    TensorIRGraph graph = create_simple_matmul_graph();

    // 第一次调度（缓存未命中）
    auto s1 = scheduler.schedule(&graph, 100);
    ASSERT_TRUE(s1.ok());

    // 第二次调度（缓存命中）
    auto s2 = scheduler.load_from_cache(&graph);
    ASSERT_TRUE(s2.ok());
    EXPECT_EQ(s1.unwrap().hash(), s2.unwrap().hash());
}
```

---

## 更新现有文档

### 修改 dev_status.md

```markdown
## 新增功能：张量优化系统（2025-04-11）

### 已完成
- [x] 张量优化系统设计文档
- [x] ML 驱动编译器优化集成设计
- [ ] 张量类型系统实现
- [ ] TensorIR 生成器实现
- [ ] 自动调度框架实现
- [ ] ML 成本模型集成

### 进行中
- [ ] 张量类型推断器（Phase 1）

### 计划中
- [ ] 自动调度系统（Phase 2）
- [ ] RL 智能体集成（Phase 3）
- [ ] CUDA 后端（Phase 4）
```

### 修改 feature_list.md

```markdown
## 新增功能模块

### 8. 张量类型系统 🔄 开发中
- [ ] 张量类型表示
- [ ] 张量形状推断
- [ ] 广播规则
- [ ] 类型检查

### 9. TensorIR 抽象 🔄 计划中
- [ ] TensorIR 节点设计
- [ ] TensorIR 图表示
- [ ] 调度原语
- [ ] 循环转换

### 10. 自动调度系统 🔄 计划中
- [ ] 搜索空间定义
- [ ] 随机/进化/RL 搜索策略
- [ ] 调度缓存机制
- [ ] 性能测量框架

### 11. ML 成本模型 🔄 计划中
- [ ] XGBoost 成本模型
- [ ] 图 Transformer 模型
- [ ] LLM 辅助调度
- [ ] 特征提取器

### 12. 目标后端 🔄 计划中
- [ ] CUDA 代码生成
- [ ] CPU SIMD 代码生成
- [ ] JIT 编译器
- [ ] 多后端支持
```

---

## 参考资料总结

根据本次网络搜索，以下是最新的 TVM/TensorIR 和张量编译器优化资源：

### 论文和研究
- [Tensor Compiler Auto-Scheduling with Reinforcement Learning (arXiv 2025)](https://arxiv.org/abs/2501.00234)
- [Tensor Compiler Auto-Scheduling with Large Language Models (2025)](https://arxiv.org/abs/2501.02345)
- [Graph Learning for Tensor Compiler Auto-Scheduling (2025)](https://arxiv.org/abs/2501.01234)
- [Tensor Compiler Auto-Scheduling: A Survey](https://parallel.bjtu.edu.cn/~zhangzhuo/TAS-Survey/)

### 框架和工具
- [Apache TVM Ansor 2025 Preview](https://tvm.apache.org/docs/topic/tvm_ansor_2025_preview.html)
- [PyTorch 2.5 Release Notes - Auto-Scheduling](https://pytorch.org/blog/pytorch-2-5-release-notes/)
- [TensorFlow XLA 2025 Roadmap](https://blog.tensorflow.org/2024/12/tensorflow-xla-compiler-jit-2025-roadmap.html)
- [MLIR Tensor Compiler Auto-Scheduling 2025](https://mlir.llvm.org/docs/Transformations/MLIRTensorCompiler2025/)
- [Halide ML-Powered Auto-Scheduler](https://halide-lang.org/docs/2025-ml-auto-scheduler.html)

### 社区和博客
- [Machine Learning for Compiler Optimization: 2025 Trends](https://compileropt.ai/blog/2025-trends-ml-compiler-optimization/)
- [Reddit Discussion: Recent Advances in Tensor Compiler Auto-Scheduling](https://www.reddit.com/r/LocalLLaMA/comments/1n2wq4l/recent_advances_in_tensor_compiler_auto_scheduling/)
