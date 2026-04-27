// feature_extractor.h - 高级特征提取器
// 从 IR / AST / TensorOp 中提取图特征、循环特征、内存特征
// 设计参考: TVM Ansor / XGBoost Cost Model / Halide Autotuner

#ifndef CLAW_FEATURE_EXTRACTOR_H
#define CLAW_FEATURE_EXTRACTOR_H

#include "ml_cost_model.h"
#include "../ir/ir.h"

#include <algorithm>
#include <numeric>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <sstream>
#include <iomanip>

namespace claw {
namespace ml {

// ============================================================================
// Graph Features - 数据流图特征
// ============================================================================

/**
 * @brief 数据流图特征
 * 
 * 描述操作之间的数据依赖关系、计算图的拓扑结构等。
 * 这些特征对理解并行性、关键路径和通信开销至关重要。
 */
struct GraphFeatures {
    // ---- 拓扑特征 ----
    int num_nodes = 0;                   // 图中节点数
    int num_edges = 0;                   // 数据依赖边数
    int dag_depth = 0;                   // 关键路径长度
    int max_fan_in = 0;                  // 最大扇入 (输入依赖)
    int max_fan_out = 0;                 // 最大扇出 (输出消费者)
    double avg_fan_in = 0.0;             // 平均扇入
    double avg_fan_out = 0.0;            // 平均扇出
    
    // ---- 并行特征 ----
    int num_parallel_groups = 0;         // 可并行执行的操作组数
    double parallelism_ratio = 0.0;      // 并行度 (并行组数 / 总节点数)
    int critical_path_ops = 0;           // 关键路径上的操作数
    
    // ---- 数据流特征 ----
    int num_broadcast_edges = 0;         // 广播边数
    int num_reduction_edges = 0;         // 归约边数
    int num_elementwise_edges = 0;       // 逐元素边数
    int num_reshape_edges = 0;           // 重塑边数
    
    // ---- 连通性特征 ----
    double connectivity = 0.0;           // 图连通性 (边数/节点数)
    int num_strongly_connected = 0;      // 强连通分量数
    double clustering_coefficient = 0.0; // 聚类系数
    
    // ---- 计算密度 ----
    double compute_to_memory_ratio = 0.0;// 计算内存比
    double total_flop_density = 0.0;     // FLOP 密度 (总FLOP/关键路径长度)
    
    // 转为特征向量
    std::vector<double> to_vector() const {
        return {
            static_cast<double>(num_nodes),
            static_cast<double>(num_edges),
            static_cast<double>(dag_depth),
            static_cast<double>(max_fan_in),
            static_cast<double>(max_fan_out),
            avg_fan_in,
            avg_fan_out,
            static_cast<double>(num_parallel_groups),
            parallelism_ratio,
            static_cast<double>(critical_path_ops),
            static_cast<double>(num_broadcast_edges),
            static_cast<double>(num_reduction_edges),
            static_cast<double>(num_elementwise_edges),
            static_cast<double>(num_reshape_edges),
            connectivity,
            static_cast<double>(num_strongly_connected),
            clustering_coefficient,
            compute_to_memory_ratio,
            total_flop_density
        };
    }
    
    static std::vector<std::string> feature_names() {
        return {
            "graph_num_nodes", "graph_num_edges", "graph_dag_depth",
            "graph_max_fan_in", "graph_max_fan_out",
            "graph_avg_fan_in", "graph_avg_fan_out",
            "graph_num_parallel_groups", "graph_parallelism_ratio",
            "graph_critical_path_ops",
            "graph_num_broadcast_edges", "graph_num_reduction_edges",
            "graph_num_elementwise_edges", "graph_num_reshape_edges",
            "graph_connectivity", "graph_num_strongly_connected",
            "graph_clustering_coefficient",
            "graph_compute_to_memory_ratio", "graph_total_flop_density"
        };
    }
};

// ============================================================================
// Loop Features - 循环嵌套特征
// ============================================================================

/**
 * @brief 单层循环特征
 */
struct LoopLevelFeatures {
    int64_t trip_count = 0;              // 循环迭代次数
    int64_t trip_count_exact = 0;        // 确切迭代次数 (如果已知)
    bool is_constant_trip = false;       // 迭代次数是否为常量
    bool is_parallelizable = false;      // 是否可并行
    bool has_carried_dependency = false; // 是否有循环携带依赖
    bool is_vectorizable = false;        // 是否可向量化
    bool is_unrollable = false;          // 是否可展开
    int64_t access_stride = 0;           // 内存访问步长 (字节)
    double spatial_locality = 0.0;       // 空间局部性 [0,1]
    double temporal_locality = 0.0;      // 时间局部性 [0,1]
    int64_t working_set_bytes = 0;       // 工作集大小
    
    std::vector<double> to_vector() const {
        return {
            static_cast<double>(trip_count),
            static_cast<double>(trip_count_exact),
            is_constant_trip ? 1.0 : 0.0,
            is_parallelizable ? 1.0 : 0.0,
            has_carried_dependency ? 1.0 : 0.0,
            is_vectorizable ? 1.0 : 0.0,
            is_unrollable ? 1.0 : 0.0,
            static_cast<double>(access_stride),
            spatial_locality,
            temporal_locality,
            static_cast<double>(working_set_bytes)
        };
    }
};

/**
 * @brief 循环嵌套特征 (多层循环)
 */
struct LoopNestFeatures {
    int nest_depth = 0;                          // 嵌套深度
    std::vector<LoopLevelFeatures> levels;       // 各层特征
    
    // 聚合特征
    int64_t total_iterations = 0;                // 总迭代次数 (各层乘积)
    int num_parallel_levels = 0;                 // 可并行层数
    int num_vectorizable_levels = 0;             // 可向量化层数
    double avg_trip_count = 0.0;                 // 平均迭代次数
    int64_t innermost_trip = 0;                  // 最内层迭代次数
    bool has_tiling_opportunity = false;          // 是否有分块机会
    double estimated_cache_miss_rate = 0.0;      // 估算缓存未命中率
    
    // ---- 内存访问模式 ----
    int64_t total_memory_accesses = 0;           // 总内存访问次数
    int64_t sequential_accesses = 0;             // 顺序访问次数
    int64_t strided_accesses = 0;                // 步长访问次数
    int64_t random_accesses = 0;                 // 随机访问次数
    double access_pattern_score = 0.0;           // 访问模式得分 [0,1]
    
    std::vector<double> to_vector() const {
        std::vector<double> features = {
            static_cast<double>(nest_depth),
            static_cast<double>(total_iterations),
            static_cast<double>(num_parallel_levels),
            static_cast<double>(num_vectorizable_levels),
            avg_trip_count,
            static_cast<double>(innermost_trip),
            has_tiling_opportunity ? 1.0 : 0.0,
            estimated_cache_miss_rate,
            static_cast<double>(total_memory_accesses),
            static_cast<double>(sequential_accesses),
            static_cast<double>(strided_accesses),
            static_cast<double>(random_accesses),
            access_pattern_score
        };
        
        // 添加各层特征 (最多取 4 层，不足补 0)
        const int max_levels = 4;
        for (int i = 0; i < max_levels; ++i) {
            if (i < static_cast<int>(levels.size())) {
                auto lv = levels[i].to_vector();
                features.insert(features.end(), lv.begin(), lv.end());
            } else {
                // 补零
                features.insert(features.end(), LoopLevelFeatures().to_vector().size(), 0.0);
            }
        }
        
        return features;
    }
    
    static std::vector<std::string> feature_names() {
        std::vector<std::string> names = {
            "loop_nest_depth", "loop_total_iterations",
            "loop_num_parallel_levels", "loop_num_vectorizable_levels",
            "loop_avg_trip_count", "loop_innermost_trip",
            "loop_has_tiling_opportunity", "loop_estimated_cache_miss_rate",
            "loop_total_memory_accesses", "loop_sequential_accesses",
            "loop_strided_accesses", "loop_random_accesses",
            "loop_access_pattern_score"
        };
        
        const int max_levels = 4;
        for (int i = 0; i < max_levels; ++i) {
            std::string prefix = "loop_l" + std::to_string(i) + "_";
            auto level_names = {"trip_count", "trip_count_exact",
                "is_constant_trip", "is_parallelizable",
                "has_carried_dependency", "is_vectorizable",
                "is_unrollable", "access_stride",
                "spatial_locality", "temporal_locality",
                "working_set_bytes"};
            for (const auto& n : level_names) {
                names.push_back(prefix + n);
            }
        }
        
        return names;
    }
};

// ============================================================================
// Memory Features - 内存层次特征
// ============================================================================

/**
 * @brief 缓存层次模型参数
 */
struct CacheHierarchyParams {
    int64_t l1_size = 32768;            // L1 缓存大小 (32 KB)
    int64_t l2_size = 262144;           // L2 缓存大小 (256 KB)
    int64_t l3_size = 8388608;          // L3 缓存大小 (8 MB)
    int64_t l1_line_size = 64;          // L1 缓存行大小
    int64_t l2_line_size = 64;          // L2 缓存行大小
    int64_t l3_line_size = 64;          // L3 缓存行大小
    double l1_latency_ns = 1.0;         // L1 延迟 (ns)
    double l2_latency_ns = 5.0;         // L2 延迟 (ns)
    double l3_latency_ns = 20.0;        // L3 延迟 (ns)
    double dram_latency_ns = 100.0;     // DRAM 延迟 (ns)
    double dram_bandwidth_gbps = 50.0;  // DRAM 带宽
    double l1_bandwidth_gbps = 500.0;   // L1 带宽
    double l2_bandwidth_gbps = 200.0;   // L2 带宽
    double l3_bandwidth_gbps = 100.0;   // L3 带宽
};

/**
 * @brief 内存层次特征
 */
struct MemoryFeatures {
    // ---- 工作集 ----
    int64_t total_data_size = 0;         // 总数据量 (字节)
    int64_t input_data_size = 0;         // 输入数据量
    int64_t output_data_size = 0;        // 输出数据量
    int64_t temp_data_size = 0;          // 临时数据量
    
    // ---- 缓存适配 ----
    double l1_fit_ratio = 0.0;          // L1 缓存适配率
    double l2_fit_ratio = 0.0;          // L2 缓存适配率
    double l3_fit_ratio = 0.0;          // L3 缓存适配率
    int primary_cache_level = 0;         // 主缓存层级 (1=L1, 2=L2, 3=L3, 4=DRAM)
    
    // ---- 访问模式 ----
    double read_write_ratio = 0.0;       // 读写比
    double sequential_ratio = 0.0;       // 顺序访问比率
    double reuse_distance_avg = 0.0;     // 平均重用距离
    double reuse_distance_max = 0.0;     // 最大重用距离
    
    // ---- 带宽利用 ----
    double estimated_bandwidth_usage = 0.0; // 估算带宽利用率
    double memory_bound_score = 0.0;     // 内存瓶颈得分 [0,1]
    
    // ---- 预取效果 ----
    double prefetch_efficiency = 0.0;    // 预取效率
    double spatial_reuse = 0.0;          // 空间重用率
    double temporal_reuse = 0.0;         // 时间重用率
    
    // ---- 估算延迟 ----
    double estimated_memory_latency_ms = 0.0; // 估算内存延迟 (ms)
    double estimated_memory_throughput_ms = 0.0; // 估算内存吞吐时间 (ms)
    
    std::vector<double> to_vector() const {
        return {
            static_cast<double>(total_data_size),
            static_cast<double>(input_data_size),
            static_cast<double>(output_data_size),
            static_cast<double>(temp_data_size),
            l1_fit_ratio, l2_fit_ratio, l3_fit_ratio,
            static_cast<double>(primary_cache_level),
            read_write_ratio, sequential_ratio,
            reuse_distance_avg, reuse_distance_max,
            estimated_bandwidth_usage, memory_bound_score,
            prefetch_efficiency, spatial_reuse, temporal_reuse,
            estimated_memory_latency_ms,
            estimated_memory_throughput_ms
        };
    }
    
    static std::vector<std::string> feature_names() {
        return {
            "mem_total_data_size", "mem_input_data_size",
            "mem_output_data_size", "mem_temp_data_size",
            "mem_l1_fit_ratio", "mem_l2_fit_ratio", "mem_l3_fit_ratio",
            "mem_primary_cache_level",
            "mem_read_write_ratio", "mem_sequential_ratio",
            "mem_reuse_distance_avg", "mem_reuse_distance_max",
            "mem_estimated_bandwidth_usage", "mem_memory_bound_score",
            "mem_prefetch_efficiency", "mem_spatial_reuse", "mem_temporal_reuse",
            "mem_estimated_memory_latency_ms",
            "mem_estimated_memory_throughput_ms"
        };
    }
};

// ============================================================================
// Composite Features - 组合特征 (用于 XGBoost)
// ============================================================================

/**
 * @brief 组合特征向量
 * 
 * 将所有特征合并为一个统一向量，用于 XGBoost 等模型训练和预测。
 * 特征排列顺序: Op基础特征 + 图特征 + 循环特征 + 内存特征
 */
struct CompositeFeatures {
    // 原始操作特征
    OpFeatures op_features;
    
    // 图特征
    GraphFeatures graph_features;
    
    // 循环嵌套特征
    LoopNestFeatures loop_features;
    
    // 内存特征
    MemoryFeatures memory_features;
    
    // 调度决策特征 (用于区分不同调度的成本)
    int num_tile_decisions = 0;
    int num_fuse_decisions = 0;
    int num_parallel_decisions = 0;
    int num_vectorize_decisions = 0;
    int num_unroll_decisions = 0;
    int64_t tile_size_product = 1;       // tile 大小乘积
    double schedule_complexity = 0.0;     // 调度复杂度得分
    
    /**
     * @brief 生成完整特征向量
     */
    std::vector<double> to_vector() const {
        std::vector<double> features;
        
        // Op 基础特征
        auto op_vec = op_features.to_feature_vector();
        features.insert(features.end(), op_vec.begin(), op_vec.end());
        
        // 图特征
        auto graph_vec = graph_features.to_vector();
        features.insert(features.end(), graph_vec.begin(), graph_vec.end());
        
        // 循环特征
        auto loop_vec = loop_features.to_vector();
        features.insert(features.end(), loop_vec.begin(), loop_vec.end());
        
        // 内存特征
        auto mem_vec = memory_features.to_vector();
        features.insert(features.end(), mem_vec.begin(), mem_vec.end());
        
        // 调度决策特征
        features.push_back(static_cast<double>(num_tile_decisions));
        features.push_back(static_cast<double>(num_fuse_decisions));
        features.push_back(static_cast<double>(num_parallel_decisions));
        features.push_back(static_cast<double>(num_vectorize_decisions));
        features.push_back(static_cast<double>(num_unroll_decisions));
        features.push_back(static_cast<double>(tile_size_product));
        features.push_back(schedule_complexity);
        
        return features;
    }
    
    /**
     * @brief 获取所有特征名
     */
    static std::vector<std::string> all_feature_names() {
        std::vector<std::string> names;
        
        auto op_names = OpFeatures::feature_names();
        names.insert(names.end(), op_names.begin(), op_names.end());
        
        auto graph_names = GraphFeatures::feature_names();
        names.insert(names.end(), graph_names.begin(), graph_names.end());
        
        auto loop_names = LoopNestFeatures::feature_names();
        names.insert(names.end(), loop_names.begin(), loop_names.end());
        
        auto mem_names = MemoryFeatures::feature_names();
        names.insert(names.end(), mem_names.begin(), mem_names.end());
        
        names.push_back("sched_num_tile");
        names.push_back("sched_num_fuse");
        names.push_back("sched_num_parallel");
        names.push_back("sched_num_vectorize");
        names.push_back("sched_num_unroll");
        names.push_back("sched_tile_size_product");
        names.push_back("sched_complexity");
        
        return names;
    }
    
    /**
     * @brief 特征维度
     */
    static size_t feature_dim() {
        return all_feature_names().size();
    }
};

// ============================================================================
// AdvancedFeatureExtractor - 高级特征提取器
// ============================================================================

/**
 * @brief 从 IR 函数中提取全面特征
 */
class AdvancedFeatureExtractor {
public:
    AdvancedFeatureExtractor();
    explicit AdvancedFeatureExtractor(const CacheHierarchyParams& cache_params);
    ~AdvancedFeatureExtractor() = default;
    
    // ====== 主接口 ======
    
    /**
     * @brief 从 IR 函数提取组合特征
     */
    CompositeFeatures extract_from_ir_function(std::shared_ptr<ir::Function> func);
    
    /**
     * @brief 从 IR 模块提取特征
     */
    std::vector<CompositeFeatures> extract_from_ir_module(std::shared_ptr<ir::Module> module);
    
    /**
     * @brief 从单个操作提取特征 (简化接口)
     */
    CompositeFeatures extract_from_op(const OpFeatures& op);
    
    // ====== 子特征提取 ======
    
    /**
     * @brief 提取数据流图特征
     */
    GraphFeatures extract_graph_features(std::shared_ptr<ir::Function> func);
    
    /**
     * @brief 提取循环嵌套特征
     */
    LoopNestFeatures extract_loop_features(std::shared_ptr<ir::Function> func);
    
    /**
     * @brief 提取内存层次特征
     */
    MemoryFeatures extract_memory_features(std::shared_ptr<ir::Function> func);
    
    // ====== 特征分析工具 ======
    
    /**
     * @brief 分析数据依赖关系
     */
    struct DataDependenceGraph {
        std::unordered_map<std::string, std::vector<std::string>> deps;     // node -> dependencies
        std::unordered_map<std::string, std::vector<std::string>> consumers; // node -> consumers
        std::unordered_map<std::string, int> topological_order;
        std::vector<std::string> critical_path;
    };
    
    DataDependenceGraph analyze_data_dependencies(std::shared_ptr<ir::Function> func);
    
    /**
     * @brief 计算关键路径
     */
    std::vector<std::string> compute_critical_path(const DataDependenceGraph& ddg);
    
    /**
     * @brief 分析内存访问模式
     */
    struct MemoryAccessPattern {
        int64_t total_accesses = 0;
        int64_t sequential = 0;
        int64_t strided = 0;
        int64_t random = 0;
        double avg_stride = 0.0;
        int64_t working_set = 0;
    };
    
    MemoryAccessPattern analyze_memory_access(std::shared_ptr<ir::Function> func);
    
    // ====== 配置 ======
    
    void set_cache_params(const CacheHierarchyParams& params);
    void set_hardware_target(HardwareTarget target);
    
    // ====== 特征归一化 ======
    
    /**
     * @brief 对特征向量做 log1p 归一化 (处理大数值范围)
     */
    static std::vector<double> normalize_features(const std::vector<double>& features);
    
    /**
     * @brief 打印特征报告
     */
    static std::string feature_report(const CompositeFeatures& features);
    
private:
    CacheHierarchyParams cache_params_;
    HardwareTarget hw_target_;
    
    // IR 分析辅助
    int64_t estimate_instruction_flops(const ir::Instruction& inst);
    int64_t estimate_instruction_memory(const ir::Instruction& inst);
    OpKind map_opcode_to_opkind(ir::OpCode opcode);
    
    // 拓扑排序
    std::vector<std::string> topological_sort(
        const std::unordered_map<std::string, std::vector<std::string>>& adj);
    
    // 计算并行组
    int count_parallel_groups(const DataDependenceGraph& ddg);
};

} // namespace ml
} // namespace claw

#endif // CLAW_FEATURE_EXTRACTOR_H
