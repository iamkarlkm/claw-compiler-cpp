// feature_extractor.cpp - 高级特征提取器实现
// 从 IR / AST / TensorOp 中提取图特征、循环特征、内存特征
// 设计参考: TVM Ansor / XGBoost Cost Model / Halide Autotuner

#include "feature_extractor.h"
#include "../ir/ir.h"

#include <algorithm>
#include <numeric>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <iostream>

namespace claw {
namespace ml {

// ============================================================================
// AdvancedFeatureExtractor - 构造/析构
// ============================================================================

AdvancedFeatureExtractor::AdvancedFeatureExtractor()
    : cache_params_()
    , hw_target_(HardwareTarget::CPU)
{
}

AdvancedFeatureExtractor::AdvancedFeatureExtractor(const CacheHierarchyParams& cache_params)
    : cache_params_(cache_params)
    , hw_target_(HardwareTarget::CPU)
{
}

// ============================================================================
// 主接口实现
// ============================================================================

CompositeFeatures AdvancedFeatureExtractor::extract_from_ir_function(
    std::shared_ptr<ir::Function> func)
{
    CompositeFeatures composite;

    if (!func) return composite;

    // 1. 从函数中提取操作级特征
    OpFeatures op;
    op.op_kind = OpKind::UNKNOWN;
    op.op_name = func->name;
    op.num_inputs = 0;
    op.num_outputs = 1;
    op.num_dims = 0;
    op.total_elements = 0;
    op.arithmetic_intensity = 0;
    op.flop_count = 0;
    op.memory_bytes = 0;
    op.input_layout = MemoryLayout::ROW_MAJOR;
    op.output_layout = MemoryLayout::ROW_MAJOR;

    // 遍历基本块收集统计信息
    int64_t total_flops = 0;
    int64_t total_mem = 0;
    int num_instructions = 0;

    for (auto& bb : func->blocks) {
        for (auto& inst : bb->instructions) {
            total_flops += estimate_instruction_flops(*inst);
            total_mem += estimate_instruction_memory(*inst);
            num_instructions++;
        }
    }

    op.flop_count = total_flops;
    op.memory_bytes = total_mem;
    op.arithmetic_intensity = (total_mem > 0) ? static_cast<int64_t>(total_flops * 1.0 / total_mem * 100) : 0;
    composite.op_features = op;

    // 2. 提取图特征
    composite.graph_features = extract_graph_features(func);

    // 3. 提取循环特征
    composite.loop_features = extract_loop_features(func);

    // 4. 提取内存特征
    composite.memory_features = extract_memory_features(func);

    return composite;
}

std::vector<CompositeFeatures> AdvancedFeatureExtractor::extract_from_ir_module(
    std::shared_ptr<ir::Module> module)
{
    std::vector<CompositeFeatures> results;
    if (!module) return results;

    for (auto& func : module->functions) {
        results.push_back(extract_from_ir_function(func));
    }

    return results;
}

CompositeFeatures AdvancedFeatureExtractor::extract_from_op(const OpFeatures& op)
{
    CompositeFeatures composite;
    composite.op_features = op;

    // 从操作特征推断图特征
    auto& gf = composite.graph_features;
    gf.num_nodes = 1;
    gf.num_edges = op.num_inputs;
    gf.dag_depth = 1;
    gf.max_fan_in = op.num_inputs;
    gf.max_fan_out = op.num_outputs;
    gf.avg_fan_in = static_cast<double>(op.num_inputs);
    gf.avg_fan_out = static_cast<double>(op.num_outputs);
    gf.num_parallel_groups = 1;
    gf.parallelism_ratio = 1.0;
    gf.critical_path_ops = 1;
    gf.connectivity = (op.num_inputs > 0) ? static_cast<double>(op.num_inputs) : 0.0;
    gf.compute_to_memory_ratio = (op.memory_bytes > 0) ?
        static_cast<double>(op.flop_count) / op.memory_bytes : 0.0;
    gf.total_flop_density = static_cast<double>(op.flop_count);

    // 推断循环特征
    auto& lf = composite.loop_features;
    lf.nest_depth = op.num_dims;
    lf.total_iterations = op.total_elements;
    lf.avg_trip_count = (op.num_dims > 0) ?
        static_cast<double>(op.total_elements) / op.num_dims : 0.0;
    lf.innermost_trip = (op.num_dims > 0) ?
        op.input_shapes.empty() ? 1 : op.input_shapes.back() : 1;

    if (op.num_dims >= 2) {
        lf.has_tiling_opportunity = true;
        LoopLevelFeatures outer;
        outer.trip_count = (op.input_shapes.size() > 0) ? op.input_shapes[0] : 1;
        outer.is_parallelizable = true;
        outer.spatial_locality = 0.7;
        outer.temporal_locality = 0.3;
        lf.levels.push_back(outer);

        LoopLevelFeatures inner;
        inner.trip_count = (op.input_shapes.size() > 1) ? op.input_shapes[1] : 1;
        inner.is_vectorizable = true;
        inner.spatial_locality = 0.9;
        inner.temporal_locality = 0.5;
        inner.access_stride = 8; // 假设 double
        lf.levels.push_back(inner);
    }

    lf.num_parallel_levels = 0;
    lf.num_vectorizable_levels = 0;
    for (auto& lv : lf.levels) {
        if (lv.is_parallelizable) lf.num_parallel_levels++;
        if (lv.is_vectorizable) lf.num_vectorizable_levels++;
    }

    // 推断内存特征
    auto& mf = composite.memory_features;
    mf.input_data_size = op.memory_bytes / std::max(op.num_inputs, 1);
    mf.output_data_size = op.memory_bytes / std::max(op.num_outputs, 1);
    mf.total_data_size = op.memory_bytes;
    mf.temp_data_size = op.memory_bytes / 4; // 估算临时数据

    mf.l1_fit_ratio = (cache_params_.l1_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l1_size) : 0.0;
    mf.l2_fit_ratio = (cache_params_.l2_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l2_size) : 0.0;
    mf.l3_fit_ratio = (cache_params_.l3_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l3_size) : 0.0;

    if (mf.total_data_size <= cache_params_.l1_size) mf.primary_cache_level = 1;
    else if (mf.total_data_size <= cache_params_.l2_size) mf.primary_cache_level = 2;
    else if (mf.total_data_size <= cache_params_.l3_size) mf.primary_cache_level = 3;
    else mf.primary_cache_level = 4;

    mf.read_write_ratio = (mf.output_data_size > 0) ?
        static_cast<double>(mf.input_data_size) / mf.output_data_size : 1.0;
    mf.sequential_ratio = 0.7; // 默认值
    mf.memory_bound_score = (op.arithmetic_intensity < 10) ? 0.8 :
                             (op.arithmetic_intensity < 50) ? 0.5 : 0.2;

    mf.estimated_memory_latency_ms = (mf.total_data_size > cache_params_.l3_size) ?
        mf.total_data_size * cache_params_.dram_latency_ns / 1e6 : 0.001;

    return composite;
}

// ============================================================================
// 图特征提取
// ============================================================================

GraphFeatures AdvancedFeatureExtractor::extract_graph_features(
    std::shared_ptr<ir::Function> func)
{
    GraphFeatures gf;
    if (!func) return gf;

    // 1. 构建数据依赖图
    auto ddg = analyze_data_dependencies(func);

    // 2. 拓扑特征
    gf.num_nodes = static_cast<int>(ddg.topological_order.size());
    gf.num_edges = 0;
    for (auto& [node, deps] : ddg.deps) {
        gf.num_edges += static_cast<int>(deps.size());
    }

    // 最大扇入/扇出
    gf.max_fan_in = 0;
    gf.max_fan_out = 0;
    for (auto& [node, deps] : ddg.deps) {
        gf.max_fan_in = std::max(gf.max_fan_in, static_cast<int>(deps.size()));
    }
    for (auto& [node, consumers] : ddg.consumers) {
        gf.max_fan_out = std::max(gf.max_fan_out, static_cast<int>(consumers.size()));
    }

    // 平均扇入/扇出
    if (gf.num_nodes > 0) {
        gf.avg_fan_in = static_cast<double>(gf.num_edges) / gf.num_nodes;
        gf.avg_fan_out = static_cast<double>(gf.num_edges) / gf.num_nodes;
    }

    // 3. DAG 深度 (关键路径长度)
    auto crit_path = compute_critical_path(ddg);
    gf.dag_depth = static_cast<int>(crit_path.size());
    gf.critical_path_ops = gf.dag_depth;

    // 4. 并行特征
    gf.num_parallel_groups = count_parallel_groups(ddg);
    gf.parallelism_ratio = (gf.num_nodes > 0) ?
        static_cast<double>(gf.num_parallel_groups) / gf.num_nodes : 0.0;

    // 5. 连通性
    gf.connectivity = (gf.num_nodes > 0) ?
        static_cast<double>(gf.num_edges) / gf.num_nodes : 0.0;

    // 6. 计算密度
    int64_t total_flops = 0;
    int64_t total_mem = 0;
    for (auto& bb : func->blocks) {
        for (auto& inst : bb->instructions) {
            total_flops += estimate_instruction_flops(*inst);
            total_mem += estimate_instruction_memory(*inst);
        }
    }
    gf.compute_to_memory_ratio = (total_mem > 0) ?
        static_cast<double>(total_flops) / total_mem : 0.0;
    gf.total_flop_density = (gf.dag_depth > 0) ?
        static_cast<double>(total_flops) / gf.dag_depth : 0.0;

    return gf;
}

// ============================================================================
// 循环特征提取
// ============================================================================

LoopNestFeatures AdvancedFeatureExtractor::extract_loop_features(
    std::shared_ptr<ir::Function> func)
{
    LoopNestFeatures lf;
    if (!func) return lf;

    // 从 IR 基本块中检测循环模式
    // 使用支配关系和回边检测循环
    std::unordered_map<std::string, bool> is_loop_header;

    int loop_count = 0;
    int64_t total_trip = 1;

    for (size_t i = 0; i < func->blocks.size(); i++) {
        auto& bb = func->blocks[i];

        // 检查是否有回边 (条件跳转到前面的块)
        for (auto& inst : bb->instructions) {
            if (inst->opcode == ir::OpCode::Br || inst->opcode == ir::OpCode::CondBr) {
                // 简化: 假设有回跳就是循环
                loop_count++;
            }
        }
    }

    lf.nest_depth = loop_count;
    lf.total_iterations = total_trip;

    if (loop_count > 0) {
        lf.avg_trip_count = static_cast<double>(total_trip) / loop_count;
        lf.has_tiling_opportunity = (loop_count >= 2);
    }

    // 从指令分析内存访问模式
    auto mem_pattern = analyze_memory_access(func);
    lf.total_memory_accesses = mem_pattern.total_accesses;
    lf.sequential_accesses = mem_pattern.sequential;
    lf.strided_accesses = mem_pattern.strided;
    lf.random_accesses = mem_pattern.random;
    lf.access_pattern_score = (mem_pattern.total_accesses > 0) ?
        static_cast<double>(mem_pattern.sequential) / mem_pattern.total_accesses : 0.0;

    // 构建循环层级特征
    for (int i = 0; i < loop_count && i < 4; i++) {
        LoopLevelFeatures lv;
        lv.trip_count = 32; // 默认估算值
        lv.is_parallelizable = true;
        lv.spatial_locality = 0.8 - i * 0.15;
        lv.temporal_locality = 0.3 + i * 0.1;
        lv.access_stride = (i == 0) ? 8 : 64;
        lf.levels.push_back(lv);

        if (lv.is_parallelizable) lf.num_parallel_levels++;
    }

    // 估算缓存未命中率 (简化 Roofline 模型)
    int64_t working_set = mem_pattern.working_set;
    if (working_set <= cache_params_.l1_size) {
        lf.estimated_cache_miss_rate = 0.02;
    } else if (working_set <= cache_params_.l2_size) {
        lf.estimated_cache_miss_rate = 0.1;
    } else if (working_set <= cache_params_.l3_size) {
        lf.estimated_cache_miss_rate = 0.3;
    } else {
        lf.estimated_cache_miss_rate = 0.8;
    }

    return lf;
}

// ============================================================================
// 内存特征提取
// ============================================================================

MemoryFeatures AdvancedFeatureExtractor::extract_memory_features(
    std::shared_ptr<ir::Function> func)
{
    MemoryFeatures mf;
    if (!func) return mf;

    // 从指令分析内存访问
    auto mem_pattern = analyze_memory_access(func);

    mf.total_data_size = mem_pattern.working_set;
    mf.input_data_size = mem_pattern.working_set * 2 / 3;   // 估算
    mf.output_data_size = mem_pattern.working_set / 3;
    mf.temp_data_size = mem_pattern.working_set / 4;

    // 缓存适配率
    mf.l1_fit_ratio = (cache_params_.l1_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l1_size) : 0.0;
    mf.l2_fit_ratio = (cache_params_.l2_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l2_size) : 0.0;
    mf.l3_fit_ratio = (cache_params_.l3_size > 0) ?
        std::min(1.0, static_cast<double>(mf.total_data_size) / cache_params_.l3_size) : 0.0;

    // 主缓存层级
    if (mf.total_data_size <= cache_params_.l1_size) mf.primary_cache_level = 1;
    else if (mf.total_data_size <= cache_params_.l2_size) mf.primary_cache_level = 2;
    else if (mf.total_data_size <= cache_params_.l3_size) mf.primary_cache_level = 3;
    else mf.primary_cache_level = 4;

    // 访问模式
    mf.sequential_ratio = (mem_pattern.total_accesses > 0) ?
        static_cast<double>(mem_pattern.sequential) / mem_pattern.total_accesses : 0.0;

    // 读写比 (估算: load vs store 指令数)
    int64_t loads = 0, stores = 0;
    for (auto& bb : func->blocks) {
        for (auto& inst : bb->instructions) {
            if (inst->opcode == ir::OpCode::Load) loads++;
            else if (inst->opcode == ir::OpCode::Store) stores++;
        }
    }
    mf.read_write_ratio = (stores > 0) ? static_cast<double>(loads) / stores : 10.0;

    // 重用距离 (基于工作集和缓存行估算)
    mf.reuse_distance_avg = (mf.l2_fit_ratio < 1.0) ?
        static_cast<double>(cache_params_.l2_size) / cache_params_.l2_line_size : 4.0;
    mf.reuse_distance_max = mf.reuse_distance_avg * 2;

    // 内存瓶颈得分
    double arith_intensity = 0.0;
    int64_t total_flops = 0;
    for (auto& bb : func->blocks) {
        for (auto& inst : bb->instructions) {
            total_flops += estimate_instruction_flops(*inst);
        }
    }
    arith_intensity = (mf.total_data_size > 0) ?
        static_cast<double>(total_flops) / mf.total_data_size : 0.0;
    mf.memory_bound_score = (arith_intensity < 5.0) ? 0.9 :
                             (arith_intensity < 20.0) ? 0.6 :
                             (arith_intensity < 50.0) ? 0.3 : 0.1;

    // 带宽利用率估算
    double roofline_bw = cache_params_.dram_bandwidth_gbps;
    double required_bw = (mf.total_data_size > 0) ?
        static_cast<double>(total_flops) / arith_intensity / 1e9 : 0.0;
    mf.estimated_bandwidth_usage = (roofline_bw > 0) ? required_bw / roofline_bw : 0.0;

    // 预取效率
    mf.prefetch_efficiency = mf.sequential_ratio * 0.8 +
                             (1.0 - mf.memory_bound_score) * 0.2;
    mf.spatial_reuse = mf.sequential_ratio * 0.9;
    mf.temporal_reuse = 1.0 - mf.memory_bound_score;

    // 内存延迟估算
    double l1_misses = mf.total_data_size * (1.0 - mf.l1_fit_ratio);
    double l2_misses = mf.total_data_size * (1.0 - mf.l2_fit_ratio);
    double l3_misses = mf.total_data_size * (1.0 - mf.l3_fit_ratio);

    mf.estimated_memory_latency_ms =
        (l1_misses * cache_params_.l2_latency_ns +
         l2_misses * cache_params_.l3_latency_ns +
         l3_misses * cache_params_.dram_latency_ns) / 1e6;

    mf.estimated_memory_throughput_ms = (roofline_bw > 0) ?
        (static_cast<double>(mf.total_data_size) / 1e9 / roofline_bw * 1000) : 0.0;

    return mf;
}

// ============================================================================
// 数据依赖分析
// ============================================================================

AdvancedFeatureExtractor::DataDependenceGraph
AdvancedFeatureExtractor::analyze_data_dependencies(
    std::shared_ptr<ir::Function> func)
{
    DataDependenceGraph ddg;
    if (!func) return ddg;

    // 遍历所有基本块和指令，构建依赖关系
    std::unordered_map<std::string, std::string> last_def; // variable -> instruction_id

    int block_idx = 0;
    for (auto& bb : func->blocks) {
        int inst_idx = 0;
        for (auto& inst : bb->instructions) {
            std::string inst_id = "bb" + std::to_string(block_idx) + "_i" + std::to_string(inst_idx);

            // 分析 use-def 链
            // LOAD: 读取变量 → 依赖变量的最后定义
            if (inst->opcode == ir::OpCode::Load) {
                std::string var_name = "var_" + std::to_string(inst_idx);
                if (last_def.count(var_name)) {
                    ddg.deps[inst_id].push_back(last_def[var_name]);
                    ddg.consumers[last_def[var_name]].push_back(inst_id);
                }
            }

            // STORE: 写入变量 → 更新最后定义
            if (inst->opcode == ir::OpCode::Store) {
                std::string var_name = "var_" + std::to_string(inst_idx);
                last_def[var_name] = inst_id;
            }

            // 算术操作: 依赖操作数
            if (inst->opcode == ir::OpCode::Add || inst->opcode == ir::OpCode::Sub ||
                inst->opcode == ir::OpCode::Mul || inst->opcode == ir::OpCode::Div) {
                // 假设操作数来自最近的 load
                for (auto& [var, def] : last_def) {
                    ddg.deps[inst_id].push_back(def);
                    ddg.consumers[def].push_back(inst_id);
                    break; // 只取最近的一个作为简化
                }
            }

            // 确保节点存在于 deps map 中
            if (!ddg.deps.count(inst_id)) {
                ddg.deps[inst_id] = {};
            }

            inst_idx++;
        }
        block_idx++;
    }

    // 拓扑排序
    auto sorted = topological_sort(ddg.deps);
    for (int i = 0; i < static_cast<int>(sorted.size()); i++) {
        ddg.topological_order[sorted[i]] = i;
    }

    return ddg;
}

// ============================================================================
// 关键路径计算
// ============================================================================

std::vector<std::string> AdvancedFeatureExtractor::compute_critical_path(
    const DataDependenceGraph& ddg)
{
    if (ddg.deps.empty()) return {};

    // 使用动态规划计算最长路径
    std::unordered_map<std::string, int> dist;
    std::unordered_map<std::string, std::string> prev;

    // 按拓扑序处理
    auto sorted = topological_sort(ddg.deps);
    for (auto& node : sorted) {
        dist[node] = 1; // 每个节点权重为 1
        prev[node] = "";
    }

    for (auto& node : sorted) {
        // 检查谁依赖这个节点
        auto it = ddg.consumers.find(node);
        if (it != ddg.consumers.end()) {
            for (auto& consumer : it->second) {
                if (dist[consumer] < dist[node] + 1) {
                    dist[consumer] = dist[node] + 1;
                    prev[consumer] = node;
                }
            }
        }
    }

    // 找到距离最大的节点
    std::string end_node;
    int max_dist = 0;
    for (auto& [node, d] : dist) {
        if (d > max_dist) {
            max_dist = d;
            end_node = node;
        }
    }

    // 回溯构建关键路径
    std::vector<std::string> path;
    std::string cur = end_node;
    while (!cur.empty()) {
        path.push_back(cur);
        cur = prev[cur];
    }
    std::reverse(path.begin(), path.end());

    return path;
}

// ============================================================================
// 内存访问模式分析
// ============================================================================

AdvancedFeatureExtractor::MemoryAccessPattern
AdvancedFeatureExtractor::analyze_memory_access(
    std::shared_ptr<ir::Function> func)
{
    MemoryAccessPattern pattern;
    if (!func) return pattern;

    std::unordered_set<std::string> accessed_vars;

    for (auto& bb : func->blocks) {
        for (auto& inst : bb->instructions) {
            if (inst->opcode == ir::OpCode::Load || inst->opcode == ir::OpCode::Store) {
                pattern.total_accesses++;
                accessed_vars.insert("v" + std::to_string(pattern.total_accesses));

                // 简化: 假设大部分 load 是顺序的
                if (pattern.total_accesses % 3 == 0) {
                    pattern.strided++;
                } else if (pattern.total_accesses % 7 == 0) {
                    pattern.random++;
                } else {
                    pattern.sequential++;
                }
            }
        }
    }

    // 估算工作集: 每个唯一变量 8 字节
    pattern.working_set = static_cast<int64_t>(accessed_vars.size()) * 8;
    pattern.avg_stride = (pattern.total_accesses > 0) ?
        static_cast<double>(pattern.working_set) / pattern.total_accesses : 0.0;

    return pattern;
}

// ============================================================================
// 辅助方法
// ============================================================================

int64_t AdvancedFeatureExtractor::estimate_instruction_flops(const ir::Instruction& inst)
{
    switch (inst.opcode) {
        // 整数算术: 1 FLOP
        case ir::OpCode::Add:
        case ir::OpCode::Sub:
        case ir::OpCode::Mul:
        case ir::OpCode::Div:
        case ir::OpCode::Mod:
            return 1;

        // 比较: 1 FLOP
        case ir::OpCode::Eq:
        case ir::OpCode::Ne:
        case ir::OpCode::Lt:
        case ir::OpCode::Le:
        case ir::OpCode::Gt:
        case ir::OpCode::Ge:
            return 1;

        // 位运算: 0.5 FLOP (通常更快)
        case ir::OpCode::And:
        case ir::OpCode::Or:
        case ir::OpCode::BitXor:
        case ir::OpCode::Shl:
        case ir::OpCode::Shr:
            return 1;

        // 内存操作: 0 FLOP (但消耗带宽)
        case ir::OpCode::Load:
        case ir::OpCode::Store:
        case ir::OpCode::Alloca:
        case ir::OpCode::GetElementPtr:
            return 0;

        // 控制流: 0 FLOP
        case ir::OpCode::Br:
        case ir::OpCode::CondBr:
        case ir::OpCode::Ret:
        case ir::OpCode::Call:
            return 0;

        default:
            return 1; // 默认估算
    }
}

int64_t AdvancedFeatureExtractor::estimate_instruction_memory(const ir::Instruction& inst)
{
    switch (inst.opcode) {
        case ir::OpCode::Load:
            return 8;  // 读取 8 字节
        case ir::OpCode::Store:
            return 8;  // 写入 8 字节
        case ir::OpCode::Alloca:
            return 64; // 栈分配估算
        case ir::OpCode::Call:
            return 32; // 函数调用开销估算
        case ir::OpCode::GetElementPtr:
            return 4;  // 地址计算
        default:
            return 0;  // 算术操作不直接访问内存
    }
}

OpKind AdvancedFeatureExtractor::map_opcode_to_opkind(ir::OpCode opcode)
{
    switch (opcode) {
        case ir::OpCode::Add: return OpKind::ADD;
        case ir::OpCode::Sub: return OpKind::SUB;
        case ir::OpCode::Mul: return OpKind::MUL;
        case ir::OpCode::Div: return OpKind::DIV;
        case ir::OpCode::Mod: return OpKind::MOD;
        case ir::OpCode::Load: return OpKind::LOAD;
        case ir::OpCode::Store: return OpKind::STORE;
        case ir::OpCode::Call: return OpKind::CALL;
        case ir::OpCode::Br: return OpKind::CONDITION;
        default: return OpKind::UNKNOWN;
    }
}

std::vector<std::string> AdvancedFeatureExtractor::topological_sort(
    const std::unordered_map<std::string, std::vector<std::string>>& adj)
{
    // Kahn 算法
    std::unordered_map<std::string, int> in_degree;
    for (auto& [node, _] : adj) {
        in_degree[node] = 0;
    }
    for (auto& [node, deps] : adj) {
        for (auto& dep : deps) {
            if (adj.count(dep)) {
                in_degree[node]++;
            }
        }
    }

    std::queue<std::string> q;
    for (auto& [node, deg] : in_degree) {
        if (deg == 0) q.push(node);
    }

    std::vector<std::string> result;
    while (!q.empty()) {
        std::string cur = q.front();
        q.pop();
        result.push_back(cur);

        auto it = adj.find(cur);
        if (it == adj.end()) continue;

        // 对于消费者 (cur 的出边指向的节点)
        // 在当前简化实现中，我们只处理直接依赖
    }

    // 如果有环，添加剩余节点
    for (auto& [node, _] : adj) {
        if (std::find(result.begin(), result.end(), node) == result.end()) {
            result.push_back(node);
        }
    }

    return result;
}

int AdvancedFeatureExtractor::count_parallel_groups(const DataDependenceGraph& ddg)
{
    if (ddg.topological_order.empty()) return 0;

    // 使用层级分析: 同一层级的节点可以并行执行
    std::unordered_map<int, int> level_counts;

    for (auto& [node, order] : ddg.topological_order) {
        int level = 0;
        auto it = ddg.deps.find(node);
        if (it != ddg.deps.end()) {
            for (auto& dep : it->second) {
                auto dep_it = ddg.topological_order.find(dep);
                if (dep_it != ddg.topological_order.end()) {
                    level = std::max(level, dep_it->second + 1);
                }
            }
        }
        level_counts[level]++;
    }

    return static_cast<int>(level_counts.size());
}

// ============================================================================
// 配置
// ============================================================================

void AdvancedFeatureExtractor::set_cache_params(const CacheHierarchyParams& params)
{
    cache_params_ = params;
}

void AdvancedFeatureExtractor::set_hardware_target(HardwareTarget target)
{
    hw_target_ = target;
    // 根据目标硬件调整缓存参数
    switch (target) {
        case HardwareTarget::CPU:
            cache_params_.l1_size = 32768;
            cache_params_.l2_size = 262144;
            cache_params_.l3_size = 8388608;
            cache_params_.dram_bandwidth_gbps = 50.0;
            break;
        case HardwareTarget::CPU_SIMD:
            cache_params_.l1_size = 32768;
            cache_params_.l2_size = 524288;
            cache_params_.l3_size = 16777216;
            cache_params_.dram_bandwidth_gbps = 60.0;
            break;
        case HardwareTarget::GPU_NVIDIA:
            cache_params_.l1_size = 65536;       // Shared memory
            cache_params_.l2_size = 4194304;     // L2 cache
            cache_params_.l3_size = 0;            // No L3
            cache_params_.dram_bandwidth_gbps = 900.0; // HBM2
            break;
        case HardwareTarget::GPU_AMD:
            cache_params_.l1_size = 65536;
            cache_params_.l2_size = 4194304;
            cache_params_.l3_size = 0;
            cache_params_.dram_bandwidth_gbps = 800.0;
            break;
        case HardwareTarget::TPU:
            cache_params_.l1_size = 0;
            cache_params_.l2_size = 0;
            cache_params_.l3_size = 0;
            cache_params_.dram_bandwidth_gbps = 600.0;
            break;
        default:
            break;
    }
}

// ============================================================================
// 特征归一化
// ============================================================================

std::vector<double> AdvancedFeatureExtractor::normalize_features(
    const std::vector<double>& features)
{
    std::vector<double> normalized;
    normalized.reserve(features.size());

    for (auto& f : features) {
        if (f >= 0) {
            // log1p 归一化: 处理大范围数值
            normalized.push_back(std::log1p(f));
        } else {
            // 负值保持不变 (通常是布尔标志)
            normalized.push_back(f);
        }
    }

    return normalized;
}

// ============================================================================
// 特征报告生成
// ============================================================================

std::string AdvancedFeatureExtractor::feature_report(const CompositeFeatures& features)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    oss << "========== Claw ML Feature Report ==========\n\n";

    // 操作特征
    oss << "--- Op Features ---\n";
    oss << "  op_name:            " << features.op_features.op_name << "\n";
    oss << "  num_inputs:         " << features.op_features.num_inputs << "\n";
    oss << "  num_outputs:        " << features.op_features.num_outputs << "\n";
    oss << "  num_dims:           " << features.op_features.num_dims << "\n";
    oss << "  total_elements:     " << features.op_features.total_elements << "\n";
    oss << "  flop_count:         " << features.op_features.flop_count << "\n";
    oss << "  memory_bytes:       " << features.op_features.memory_bytes << "\n";
    oss << "  arithmetic_intensity: " << features.op_features.arithmetic_intensity << "\n\n";

    // 图特征
    auto& gf = features.graph_features;
    oss << "--- Graph Features ---\n";
    oss << "  num_nodes:          " << gf.num_nodes << "\n";
    oss << "  num_edges:          " << gf.num_edges << "\n";
    oss << "  dag_depth:          " << gf.dag_depth << "\n";
    oss << "  max_fan_in:         " << gf.max_fan_in << "\n";
    oss << "  max_fan_out:        " << gf.max_fan_out << "\n";
    oss << "  parallel_groups:    " << gf.num_parallel_groups << "\n";
    oss << "  parallelism_ratio:  " << gf.parallelism_ratio << "\n";
    oss << "  connectivity:       " << gf.connectivity << "\n";
    oss << "  compute_to_mem:     " << gf.compute_to_memory_ratio << "\n\n";

    // 循环特征
    auto& lf = features.loop_features;
    oss << "--- Loop Features ---\n";
    oss << "  nest_depth:         " << lf.nest_depth << "\n";
    oss << "  total_iterations:   " << lf.total_iterations << "\n";
    oss << "  parallel_levels:    " << lf.num_parallel_levels << "\n";
    oss << "  vectorizable_levels: " << lf.num_vectorizable_levels << "\n";
    oss << "  has_tiling:         " << (lf.has_tiling_opportunity ? "yes" : "no") << "\n";
    oss << "  est_cache_miss:     " << lf.estimated_cache_miss_rate << "\n";
    oss << "  mem_accesses:       " << lf.total_memory_accesses << "\n";
    oss << "  sequential_ratio:   " << (lf.total_memory_accesses > 0 ?
        static_cast<double>(lf.sequential_accesses) / lf.total_memory_accesses : 0.0) << "\n\n";

    // 内存特征
    auto& mf = features.memory_features;
    oss << "--- Memory Features ---\n";
    oss << "  total_data_size:    " << mf.total_data_size << " bytes\n";
    oss << "  l1_fit_ratio:       " << mf.l1_fit_ratio << "\n";
    oss << "  l2_fit_ratio:       " << mf.l2_fit_ratio << "\n";
    oss << "  l3_fit_ratio:       " << mf.l3_fit_ratio << "\n";
    oss << "  primary_cache:      L" << mf.primary_cache_level << "\n";
    oss << "  read_write_ratio:   " << mf.read_write_ratio << "\n";
    oss << "  mem_bound_score:    " << mf.memory_bound_score << "\n";
    oss << "  prefetch_efficiency: " << mf.prefetch_efficiency << "\n";
    oss << "  est_latency_ms:     " << mf.estimated_memory_latency_ms << "\n\n";

    // 调度特征
    oss << "--- Schedule Features ---\n";
    oss << "  num_tile:           " << features.num_tile_decisions << "\n";
    oss << "  num_fuse:           " << features.num_fuse_decisions << "\n";
    oss << "  num_parallel:       " << features.num_parallel_decisions << "\n";
    oss << "  num_vectorize:      " << features.num_vectorize_decisions << "\n";
    oss << "  num_unroll:         " << features.num_unroll_decisions << "\n";
    oss << "  schedule_complexity: " << features.schedule_complexity << "\n\n";

    // 特征维度统计
    auto vec = features.to_vector();
    oss << "--- Summary ---\n";
    oss << "  total_feature_dim:  " << vec.size() << "\n";
    oss << "=============================================\n";

    return oss.str();
}

} // namespace ml
} // namespace claw
