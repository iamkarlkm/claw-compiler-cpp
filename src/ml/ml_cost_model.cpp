#include "ml_cost_model.h"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <sstream>

namespace claw {
namespace ml {

// ============================================================================
// OpFeatures Implementation
// ============================================================================

OpFeatures::OpFeatures()
    : op_kind(OpKind::UNKNOWN), op_name(""), num_inputs(0), num_outputs(0),
      num_dims(0), total_elements(0), arithmetic_intensity(0),
      flop_count(0), memory_bytes(0), input_layout(MemoryLayout::ROW_MAJOR),
      output_layout(MemoryLayout::ROW_MAJOR), num_loops(0), loop_nest_depth(0),
      has_reduction(false), has_broadcast(false), target(HardwareTarget::CPU),
      is_parallelizable(true) {}

std::vector<double> OpFeatures::to_feature_vector() const {
    std::vector<double> features;
    
    // OpKind one-hot encoding (18 values)
    for (int i = 0; i < 18; ++i) {
        features.push_back(static_cast<int>(op_kind) == i ? 1.0 : 0.0);
    }
    
    // 基本数值特征
    features.push_back(static_cast<double>(num_inputs));
    features.push_back(static_cast<double>(num_outputs));
    features.push_back(static_cast<double>(num_dims));
    features.push_back(static_cast<double>(total_elements));
    features.push_back(static_cast<double>(arithmetic_intensity));
    features.push_back(static_cast<double>(flop_count));
    features.push_back(static_cast<double>(memory_bytes));
    features.push_back(static_cast<double>(num_loops));
    features.push_back(static_cast<double>(loop_nest_depth));
    features.push_back(has_reduction ? 1.0 : 0.0);
    features.push_back(has_broadcast ? 1.0 : 0.0);
    features.push_back(is_parallelizable ? 1.0 : 0.0);
    
    // 布局 one-hot
    for (int i = 0; i < 5; ++i) {
        features.push_back(static_cast<int>(input_layout) == i ? 1.0 : 0.0);
    }
    for (int i = 0; i < 5; ++i) {
        features.push_back(static_cast<int>(output_layout) == i ? 1.0 : 0.0);
    }
    
    // 硬件 one-hot
    for (int i = 0; i < 5; ++i) {
        features.push_back(static_cast<int>(target) == i ? 1.0 : 0.0);
    }
    
    return features;
}

std::vector<std::string> OpFeatures::feature_names() {
    std::vector<std::string> names;
    
    // OpKind
    std::vector<std::string> op_kinds = {"ADD", "SUB", "MUL", "DIV", "MOD",
        "SUM", "MEAN", "MAX", "MIN", "ARGMAX", "ARGMIN", "MATMUL", "TENSORDOT",
        "TRANSPOSE", "RESHAPE", "SLICE", "CONCAT", "SPLIT"};
    for (const auto& k : op_kinds) names.push_back("op_" + k);
    
    // 基本特征
    names.push_back("num_inputs");
    names.push_back("num_outputs");
    names.push_back("num_dims");
    names.push_back("total_elements");
    names.push_back("arithmetic_intensity");
    names.push_back("flop_count");
    names.push_back("memory_bytes");
    names.push_back("num_loops");
    names.push_back("loop_nest_depth");
    names.push_back("has_reduction");
    names.push_back("has_broadcast");
    names.push_back("is_parallelizable");
    
    // Layout one-hot
    for (const auto& l : {"ROW_MAJOR", "COL_MAJOR", "NHWC", "NCHW", "CHWN"}) {
        names.push_back("input_layout_" + std::string(l));
    }
    for (const auto& l : {"ROW_MAJOR", "COL_MAJOR", "NHWC", "NCHW", "CHWN"}) {
        names.push_back("output_layout_" + std::string(l));
    }
    
    // Hardware
    for (const auto& h : {"CPU", "GPU_NVIDIA", "GPU_AMD", "TPU", "CPU_SIMD"}) {
        names.push_back("target_" + std::string(h));
    }
    
    return names;
}

// ============================================================================
// ModuleFeatures Implementation
// ============================================================================

ModuleFeatures::ModuleFeatures()
    : total_ops(0), total_flops(0), total_memory_bytes(0),
      avg_arithmetic_intensity(0), max_loop_nest_depth(0),
      num_reductions(0), num_broadcasts(0), dag_depth(0),
      num_parallel_chains(0), parallelism_ratio(0),
      peak_memory_estimate(0), memory_footprint(0) {}

ModuleFeatures::ModuleFeatures(const std::vector<OpFeatures>& ops)
    : op_features(ops) {
    if (ops.empty()) return;
    
    total_ops = static_cast<int>(ops.size());
    total_flops = 0;
    total_memory_bytes = 0;
    int64_t total_elements = 0;
    max_loop_nest_depth = 0;
    num_reductions = 0;
    num_broadcasts = 0;
    
    for (const auto& op : ops) {
        total_flops += op.flop_count;
        total_memory_bytes += op.memory_bytes;
        total_elements += op.total_elements;
        max_loop_nest_depth = std::max(max_loop_nest_depth, op.loop_nest_depth);
        if (op.has_reduction) num_reductions++;
        if (op.has_broadcast) num_broadcasts++;
    }
    
    avg_arithmetic_intensity = total_memory_bytes > 0 ?
        static_cast<double>(total_flops) / total_memory_bytes : 0;
    
    // DAG 深度估计 (简化版)
    dag_depth = max_loop_nest_depth + num_reductions;
    
    // 并行化比率
    int parallelizable = 0;
    for (const auto& op : ops) {
        if (op.is_parallelizable) parallelizable++;
    }
    parallelism_ratio = total_ops > 0 ?
        static_cast<double>(parallelizable) / total_ops : 0;
    
    // 内存估算
    memory_footprint = total_memory_bytes;
    peak_memory_estimate = static_cast<int>(total_elements * 8); // 假设 8 字节/元素
}

std::vector<double> ModuleFeatures::to_feature_vector() const {
    std::vector<double> features;
    
    // 聚合统计
    features.push_back(static_cast<double>(total_ops));
    features.push_back(static_cast<double>(total_flops));
    features.push_back(static_cast<double>(total_memory_bytes));
    features.push_back(avg_arithmetic_intensity);
    features.push_back(static_cast<double>(max_loop_nest_depth));
    features.push_back(static_cast<double>(num_reductions));
    features.push_back(static_cast<double>(num_broadcasts));
    features.push_back(static_cast<double>(dag_depth));
    features.push_back(static_cast<double>(num_parallel_chains));
    features.push_back(parallelism_ratio);
    features.push_back(static_cast<double>(peak_memory_estimate));
    features.push_back(static_cast<double>(memory_footprint));
    
    // 添加每个操作的特征 (取平均)
    if (!op_features.empty()) {
        size_t feature_dim = op_features[0].to_feature_vector().size();
        std::vector<double> avg_features(feature_dim, 0);
        for (const auto& op : op_features) {
            auto fv = op.to_feature_vector();
            for (size_t i = 0; i < feature_dim; ++i) {
                avg_features[i] += fv[i];
            }
        }
        for (auto& f : avg_features) {
            f /= op_features.size();
            features.push_back(f);
        }
    }
    
    return features;
}

// ============================================================================
// Feature Extractor Implementation
// ============================================================================

int64_t FeatureExtractor::calc_total_elements(const std::vector<int64_t>& shape) {
    int64_t total = 1;
    for (int64_t dim : shape) {
        total *= dim;
    }
    return total;
}

int64_t FeatureExtractor::estimate_flops(OpKind kind, const std::vector<int64_t>& output_shape) {
    int64_t elements = calc_total_elements(output_shape);
    switch (kind) {
        case OpKind::ADD:
        case OpKind::SUB:
            return elements;  // 1 FLOP per element
        case OpKind::MUL:
            return elements;
        case OpKind::DIV:
            return elements * 4;  // Division is more expensive
        case OpKind::MATMUL:
            // For matmul of (M, K) × (K, N) = (M, N)
            if (output_shape.size() >= 2) {
                return elements * output_shape[output_shape.size() - 1];
            }
            return elements * 100;
        case OpKind::SUM:
        case OpKind::MEAN:
        case OpKind::MAX:
        case OpKind::MIN:
            return elements;  // Reduction
        case OpKind::CONV2D:
            // Simplified conv FLOPs
            return elements * 9 * output_shape.size();
        default:
            return elements;
    }
}

int64_t FeatureExtractor::estimate_memory(OpKind kind, const std::vector<int64_t>& input_shape) {
    int64_t elements = calc_total_elements(input_shape);
    return elements * 8;  // 8 bytes per element (float64)
}

OpFeatures StandardFeatureExtractor::extract(
    const std::string& op_name, OpKind kind,
    const std::vector<std::vector<int64_t>>& input_shapes,
    const std::vector<std::vector<int64_t>>& output_shapes) {
    
    OpFeatures features;
    features.op_name = op_name;
    features.op_kind = kind;
    features.num_inputs = static_cast<int>(input_shapes.size());
    features.num_outputs = static_cast<int>(output_shapes.size());
    features.input_shapes = input_shapes.empty() ? std::vector<int64_t>() : input_shapes[0];
    features.output_shapes = output_shapes.empty() ? std::vector<int64_t>() : output_shapes[0];
    
    // 维度计算
    features.num_dims = static_cast<int>(features.output_shapes.size());
    features.total_elements = calc_total_elements(features.output_shapes);
    
    // 计算特征
    features.flop_count = estimate_flops(kind, features.output_shapes);
    features.memory_bytes = estimate_memory(kind, features.input_shapes);
    
    // 算术强度
    features.arithmetic_intensity = features.memory_bytes > 0 ?
        features.flop_count / features.memory_bytes : 0;
    
    // 检查 broadcast
    features.has_broadcast = false;
    for (size_t i = 0; i < input_shapes.size(); ++i) {
        if (!input_shapes[i].empty() && !output_shapes.empty()) {
            if (input_shapes[i].size() != output_shapes.size()) {
                features.has_broadcast = true;
                break;
            }
        }
    }
    
    // 检查 reduction
    switch (kind) {
        case OpKind::SUM:
        case OpKind::MEAN:
        case OpKind::MAX:
        case OpKind::MIN:
        case OpKind::ARGMAX:
        case OpKind::ARGMIN:
            features.has_reduction = true;
            break;
        default:
            features.has_reduction = false;
    }
    
    return features;
}

ModuleFeatures StandardFeatureExtractor::extract_module(const std::vector<OpFeatures>& ops) {
    return ModuleFeatures(ops);
}

// ============================================================================
// HeuristicCostModel Implementation
// ============================================================================

HeuristicCostModel::HeuristicCostModel()
    : cpu_peak_gflops_(500.0),      // 500 GFLOPS (modern CPU)
      memory_bandwidth_gbps_(50.0), // 50 GB/s
      cache_bandwidth_gbps_(200.0), // 200 GB/s (L3 cache)
      memory_latency_ns_(100.0),    // 100 ns
      operation_overhead_ns_(10.0)  // 10 ns
{}

double HeuristicCostModel::compute_flops_cost(const OpFeatures& f) const {
    if (f.flop_count == 0) return 0;
    double gflops = static_cast<double>(f.flop_count);
    return gflops / cpu_peak_gflops_;  // seconds
}

double HeuristicCostModel::compute_memory_cost(const OpFeatures& f) const {
    if (f.memory_bytes == 0) return 0;
    double bytes = static_cast<double>(f.memory_bytes);
    return bytes / (memory_bandwidth_gbps_ * 1e9);  // seconds
}

double HeuristicCostModel::compute_bottleneck_cost(const OpFeatures& f) const {
    double flops_cost = compute_flops_cost(f);
    double mem_cost = compute_memory_cost(f);
    
    // 瓶颈是两者中较大的那个
    double bottleneck = std::max(flops_cost, mem_cost);
    
    // 如果算术强度很高，瓶颈在 FLOPs
    // 如果算术强度很低，瓶颈在内存
    // 添加一些开销
    double overhead = operation_overhead_ns_ * 1e-9 * f.total_elements;
    
    return bottleneck + overhead;
}

double HeuristicCostModel::predict(const OpFeatures& features) {
    // 基本成本 = 瓶颈成本 + 循环开销
    double base_cost = compute_bottleneck_cost(features);
    
    // 循环嵌套开销
    double loop_overhead = 0;
    if (features.loop_nest_depth > 1) {
        loop_overhead = features.loop_nest_depth * 1e-6; // 1us per nest level
    }
    
    // Reduction 开销
    double reduction_overhead = features.has_reduction ? 1e-5 : 0;
    
    // Broadcast 开销
    double broadcast_overhead = features.has_broadcast ? 5e-6 : 0;
    
    // 转换到毫秒
    double total_cost_ms = (base_cost + loop_overhead + reduction_overhead + 
                           broadcast_overhead) * 1000.0;
    
    return std::max(0.001, total_cost_ms);  // 至少 1 微秒
}

double HeuristicCostModel::predict_module(const ModuleFeatures& features) {
    if (features.op_features.empty()) return 0;
    
    double total_time = 0;
    for (const auto& op : features.op_features) {
        total_time += predict(op);
    }
    
    // 考虑并行化
    if (features.parallelism_ratio > 0.5) {
        total_time *= (1.0 / features.parallelism_ratio);
    }
    
    return total_time;
}

void HeuristicCostModel::train(const std::vector<std::pair<OpFeatures, double>>& data) {
    // 启发式模型不需要训练，但可以调整基准参数
    (void)data;
}

void HeuristicCostModel::train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) {
    (void)data;
}

void HeuristicCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "heuristic\n";
    out << cpu_peak_gflops_ << "\n";
    out << memory_bandwidth_gbps_ << "\n";
    out << memory_latency_ns_ << "\n";
    out << operation_overhead_ns_ << "\n";
}

void HeuristicCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    in >> cpu_peak_gflops_;
    in >> memory_bandwidth_gbps_;
    in >> memory_latency_ns_;
    in >> operation_overhead_ns_;
}

std::string HeuristicCostModel::model_info() const {
    std::ostringstream oss;
    oss << "HeuristicCostModel[";
    oss << "GFLOPS=" << cpu_peak_gflops_ << ",";
    oss << "BW=" << memory_bandwidth_gbps_ << "GB/s]";
    return oss.str();
}

// ============================================================================
// LinearRegressionCostModel Implementation
// ============================================================================

LinearRegressionCostModel::LinearRegressionCostModel()
    : bias_(0), trained_(false), learning_rate_(0.01), max_iterations_(1000) {}

double LinearRegressionCostModel::predict_vector(const std::vector<double>& features) const {
    if (!trained_ || weights_.empty()) {
        // Return simple heuristic fallback based on FLOPs
        if (features.size() > 5) {
            double flops = features[5];
            return flops / 1e6 + 0.1;  // Simple estimate: 1 GFLOP = 1ms
        }
        return 1.0;  // Default 1ms
    }
    
    double result = bias_;
    for (size_t i = 0; i < features.size() && i < weights_.size(); ++i) {
        result += features[i] * weights_[i];
    }
    return std::isnan(result) ? 1.0 : std::max(0.001, result);
}

double LinearRegressionCostModel::predict(const OpFeatures& features) {
    auto fv = features.to_feature_vector();
    return predict_vector(fv);
}

double LinearRegressionCostModel::predict_module(const ModuleFeatures& features) {
    auto fv = features.to_feature_vector();
    return predict_vector(fv);
}

void LinearRegressionCostModel::train_gradient_descent(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& y) {
    
    if (X.empty() || y.empty()) return;
    
    size_t n_features = X[0].size();
    if (n_features == 0) return;
    
    weights_.assign(n_features, 0.0);
    bias_ = 0;
    
    for (int iter = 0; iter < max_iterations_; ++iter) {
        std::vector<double> gradients(n_features, 0);
        double bias_grad = 0;
        double loss = 0;
        
        for (size_t i = 0; i < X.size(); ++i) {
            // 预测
            double pred = bias_;
            for (size_t j = 0; j < n_features; ++j) {
                pred += X[i][j] * weights_[j];
            }
            
            // 计算梯度
            double error = pred - y[i];
            loss += error * error;
            
            bias_grad += error;
            for (size_t j = 0; j < n_features; ++j) {
                gradients[j] += error * X[i][j];
            }
        }
        
        // 更新
        double scale = 1.0 / X.size();
        bias_ -= learning_rate_ * bias_grad * scale;
        for (size_t j = 0; j < n_features; ++j) {
            weights_[j] -= learning_rate_ * gradients[j] * scale;
        }
        
        // 检查收敛
        if (loss / X.size() < 1e-6) break;
    }
    
    trained_ = true;
}

void LinearRegressionCostModel::train(
    const std::vector<std::pair<OpFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    if (!X.empty()) {
        train_gradient_descent(X, y);
    }
}

void LinearRegressionCostModel::train_module(
    const std::vector<std::pair<ModuleFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    if (!X.empty()) {
        train_gradient_descent(X, y);
    }
}

void LinearRegressionCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "linear_regression\n";
    out << trained_ << "\n";
    out << weights_.size() << "\n";
    for (double w : weights_) out << w << " ";
    out << "\n" << bias_ << "\n";
}

void LinearRegressionCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    in >> trained_;
    size_t n;
    in >> n;
    weights_.resize(n);
    for (size_t i = 0; i < n; ++i) in >> weights_[i];
    in >> bias_;
}

std::string LinearRegressionCostModel::model_info() const {
    std::ostringstream oss;
    oss << "LinearRegression[weights=" << weights_.size() 
        << ",trained=" << trained_ << "]";
    return oss.str();
}

// ============================================================================
// XGBoostCostModel Implementation
// ============================================================================

XGBoostCostModel::XGBoostCostModel()
    : trained_(false), num_trees_(10), max_depth_(3), 
      learning_rate_(0.1), min_gain_(1e-4), regularization_(1.0) {}

double XGBoostCostModel::predict_tree(
    const std::vector<TreeNode>& tree,
    const std::vector<double>& features) const {
    
    int node = 0;
    while (!tree[node].is_leaf) {
        double value = features[tree[node].feature_index];
        if (value <= tree[node].threshold) {
            node = tree[node].left_child;
        } else {
            node = tree[node].right_child;
        }
    }
    return tree[node].value;
}

double XGBoostCostModel::predict(const OpFeatures& features) {
    if (!trained_) {
        HeuristicCostModel hm;
        return hm.predict(features);
    }
    
    auto fv = features.to_feature_vector();
    double result = 0;
    for (size_t i = 0; i < trees_.size(); ++i) {
        result += tree_weights_[i] * predict_tree(trees_[i], fv);
    }
    return result > 0 ? result : 1.0;
}

double XGBoostCostModel::predict_module(const ModuleFeatures& features) {
    if (!trained_) {
        return HeuristicCostModel().predict_module(features);
    }
    
    auto fv = features.to_feature_vector();
    double result = 0;
    for (size_t i = 0; i < trees_.size(); ++i) {
        result += tree_weights_[i] * predict_tree(trees_[i], fv);
    }
    return result;
}

std::vector<TreeNode> XGBoostCostModel::build_tree(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& residuals,
    int depth) {
    
    if (depth >= max_depth_ || X.empty()) {
        // 叶子节点：返回残差均值
        TreeNode leaf;
        leaf.is_leaf = true;
        leaf.value = std::accumulate(residuals.begin(), residuals.end(), 0.0) / 
                     std::max(1, static_cast<int>(residuals.size()));
        return {leaf};
    }
    
    // 找到最佳分割点
    int best_feature = -1;
    double best_threshold = 0;
    double best_gain = 0;
    
    size_t n_features = X[0].size();
    for (size_t f = 0; f < n_features && f < 20; ++f) {  // 限制搜索
        // 收集该特征的所有值
        std::vector<std::pair<double, double>> values;  // (feature_value, residual)
        for (size_t i = 0; i < X.size(); ++i) {
            values.push_back({X[i][f], residuals[i]});
        }
        
        // 排序
        std::sort(values.begin(), values.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        
        // 尝试分割点
        for (size_t i = 1; i < values.size(); ++i) {
            double threshold = (values[i-1].first + values[i].first) / 2;
            
            // 计算增益 (简化版)
            double left_sum = 0, right_sum = 0;
            int left_n = 0, right_n = 0;
            for (size_t j = 0; j < values.size(); ++j) {
                if (values[j].first <= threshold) {
                    left_sum += values[j].second;
                    left_n++;
                } else {
                    right_sum += values[j].second;
                    right_n++;
                }
            }
            
            double left_mean = left_n > 0 ? left_sum / left_n : 0;
            double right_mean = right_n > 0 ? right_sum / right_n : 0;
            
            // 简化增益
            double gain = std::abs(left_mean - right_mean);
            if (gain > best_gain) {
                best_gain = gain;
                best_feature = static_cast<int>(f);
                best_threshold = threshold;
            }
        }
    }
    
    if (best_feature < 0 || best_gain < min_gain_) {
        // 叶子
        TreeNode leaf;
        leaf.is_leaf = true;
        leaf.value = std::accumulate(residuals.begin(), residuals.end(), 0.0) /
                     std::max(1, static_cast<int>(residuals.size()));
        return {leaf};
    }
    
    // 分割
    std::vector<std::vector<double>> left_X, right_X;
    std::vector<double> left_y, right_y;
    
    for (size_t i = 0; i < X.size(); ++i) {
        if (X[i][best_feature] <= best_threshold) {
            left_X.push_back(X[i]);
            left_y.push_back(residuals[i]);
        } else {
            right_X.push_back(X[i]);
            right_y.push_back(residuals[i]);
        }
    }
    
    // 构建子树
    std::vector<TreeNode> left_tree = build_tree(left_X, left_y, depth + 1);
    std::vector<TreeNode> right_tree = build_tree(right_X, right_y, depth + 1);
    
    // 组合
    TreeNode root;
    root.is_leaf = false;
    root.feature_index = best_feature;
    root.threshold = best_threshold;
    root.left_child = 1;  // left tree starts at index 1
    root.right_child = 1 + static_cast<int>(left_tree.size());
    
    std::vector<TreeNode> result;
    result.push_back(root);
    result.insert(result.end(), left_tree.begin(), left_tree.end());
    result.insert(result.end(), right_tree.begin(), right_tree.end());
    
    return result;
}

void XGBoostCostModel::train_boosting(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& y) {
    
    if (X.empty()) return;
    
    trees_.clear();
    tree_weights_.clear();
    
    // 初始化预测
    std::vector<double> predictions(X.size(), 0);
    std::vector<double> residuals = y;
    
    for (int t = 0; t < num_trees_; ++t) {
        // 构建树
        std::vector<TreeNode> tree = build_tree(X, residuals, 0);
        trees_.push_back(tree);
        
        // 计算树的输出
        std::vector<double> tree_pred(X.size());
        for (size_t i = 0; i < X.size(); ++i) {
            tree_pred[i] = predict_tree(tree, X[i]);
        }
        
        // 更新预测和残差
        double weight = learning_rate_;
        tree_weights_.push_back(weight);
        
        for (size_t i = 0; i < X.size(); ++i) {
            predictions[i] += weight * tree_pred[i];
            residuals[i] = y[i] - predictions[i];
        }
    }
    
    trained_ = true;
}

void XGBoostCostModel::train(
    const std::vector<std::pair<OpFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    if (!X.empty()) {
        train_boosting(X, y);
    }
}

void XGBoostCostModel::train_module(
    const std::vector<std::pair<ModuleFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    if (!X.empty()) {
        train_boosting(X, y);
    }
}

void XGBoostCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "xgboost\n";
    out << trained_ << "\n";
    out << num_trees_ << "\n";
    out << max_depth_ << "\n";
    out << learning_rate_ << "\n";
    // Save trees (simplified)
    out << trees_.size() << "\n";
}

void XGBoostCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    in >> trained_;
    in >> num_trees_;
    in >> max_depth_;
    in >> learning_rate_;
    size_t n_trees;
    in >> n_trees;
    // Load trees (simplified)
}

std::string XGBoostCostModel::model_info() const {
    std::ostringstream oss;
    oss << "XGBoost[trees=" << num_trees_ << ",depth=" << max_depth_ 
        << ",lr=" << learning_rate_ << ",trained=" << trained_ << "]";
    return oss.str();
}

// ============================================================================
// NeuralNetworkCostModel Implementation
// ============================================================================

NeuralNetworkCostModel::NeuralNetworkCostModel() 
    : trained_(false), learning_rate_(0.01), epochs_(100), batch_size_(32) {
    // 默认: 输入 -> 64 -> 32 -> 1
    layer_sizes_ = {64, 32, 1};
}

NeuralNetworkCostModel::NeuralNetworkCostModel(const std::vector<int>& layer_sizes)
    : layer_sizes_(layer_sizes), trained_(false), 
      learning_rate_(0.01), epochs_(100), batch_size_(32) {
    
    // 初始化权重
    for (size_t i = 0; i < layer_sizes_.size() - 1; ++i) {
        int in = layer_sizes_[i];
        int out = layer_sizes_[i + 1];
        
        weights_.push_back(std::vector<double>(in * out, 0.01));
        biases_.push_back(std::vector<double>(out, 0));
    }
}

NeuralNetworkCostModel::~NeuralNetworkCostModel() = default;

std::vector<double> NeuralNetworkCostModel::forward(
    const std::vector<double>& input) const {
    
    std::vector<double> current = input;
    
    for (size_t l = 0; l < weights_.size(); ++l) {
        const auto& W = weights_[l];
        const auto& b = biases_[l];
        int out_size = static_cast<int>(b.size());
        
        std::vector<double> output(out_size, 0);
        
        for (int i = 0; i < out_size; ++i) {
            output[i] = b[i];
            for (size_t j = 0; j < current.size(); ++j) {
                output[i] += current[j] * W[j * out_size + i];
            }
            // ReLU 激活 (除了输出层)
            if (l < weights_.size() - 1) {
                output[i] = relu(output[i]);
            }
        }
        current = output;
    }
    
    return current;
}

void NeuralNetworkCostModel::backward(
    const std::vector<double>& input,
    const std::vector<double>& target) {
    
    // 前向传播
    std::vector<std::vector<double>> activations;
    std::vector<double> current = input;
    activations.push_back(current);
    
    for (size_t l = 0; l < weights_.size(); ++l) {
        const auto& W = weights_[l];
        const auto& b = biases_[l];
        int out_size = static_cast<int>(b.size());
        
        std::vector<double> output(out_size);
        for (int i = 0; i < out_size; ++i) {
            output[i] = b[i];
            for (size_t j = 0; j < current.size(); ++j) {
                output[i] += current[j] * W[j * out_size + i];
            }
            if (l < weights_.size() - 1) {
                output[i] = relu(output[i]);
            }
        }
        current = output;
        activations.push_back(current);
    }
    
    // 输出层误差
    std::vector<double> delta = current;
    for (size_t i = 0; i < delta.size(); ++i) {
        delta[i] = delta[i] - target[0];  // MSE 导数
    }
    
    // 反向传播
    for (int l = static_cast<int>(weights_.size()) - 1; l >= 0; --l) {
        int in_size = layer_sizes_[l];
        int out_size = layer_sizes_[l + 1];
        
        // 计算权重梯度
        std::vector<double> grad_w(in_size * out_size, 0);
        std::vector<double> grad_b(out_size, 0);
        
        for (int i = 0; i < out_size; ++i) {
            grad_b[i] = delta[i];
            for (int j = 0; j < in_size; ++j) {
                grad_w[j * out_size + i] = delta[i] * activations[l][j];
            }
        }
        
        // 更新权重
        for (int i = 0; i < out_size; ++i) {
            biases_[l][i] -= learning_rate_ * grad_b[i];
            for (int j = 0; j < in_size; ++j) {
                weights_[l][j * out_size + i] -= learning_rate_ * grad_w[j * out_size + i];
            }
        }
        
        // 传播误差到下一层
        if (l > 0) {
            std::vector<double> prev_delta(in_size, 0);
            for (int i = 0; i < out_size; ++i) {
                for (int j = 0; j < in_size; ++j) {
                    prev_delta[j] += delta[i] * weights_[l][j * out_size + i] * 
                                    relu_derivative(activations[l][j]);
                }
            }
            delta = prev_delta;
        }
    }
}

void NeuralNetworkCostModel::train_batch(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& y) {
    
    for (size_t i = 0; i < X.size(); ++i) {
        backward(X[i], {y[i]});
    }
}

double NeuralNetworkCostModel::predict(const OpFeatures& features) {
    if (!trained_) {
        double flops = static_cast<double>(features.flop_count);
        return flops / 1e6 + 0.1;
    }
    auto fv = features.to_feature_vector();
    auto result = forward(fv);
    double pred = result.empty() ? 1.0 : result[0];
    return std::isnan(pred) ? 1.0 : std::max(0.001, pred);
}

double NeuralNetworkCostModel::predict_module(const ModuleFeatures& features) {
    if (!trained_) {
        return features.total_flops / 1e6 + 0.1;
    }
    auto fv = features.to_feature_vector();
    auto result = forward(fv);
    double pred = result.empty() ? 1.0 : result[0];
    return std::isnan(pred) ? 1.0 : std::max(0.001, pred);
}

void NeuralNetworkCostModel::train(
    const std::vector<std::pair<OpFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    if (X.empty()) return;
    
    // 调整网络结构以适应输入
    int input_dim = static_cast<int>(X[0].size());
    if (layer_sizes_.empty() || layer_sizes_[0] != input_dim) {
        layer_sizes_.insert(layer_sizes_.begin(), input_dim);
        // 重建权重
        weights_.clear();
        biases_.clear();
        for (size_t i = 0; i < layer_sizes_.size() - 1; ++i) {
            int in = layer_sizes_[i];
            int out = layer_sizes_[i + 1];
            weights_.push_back(std::vector<double>(in * out, 0.01));
            biases_.push_back(std::vector<double>(out, 0));
        }
    }
    
    // 训练
    for (int e = 0; e < epochs_; ++e) {
        train_batch(X, y);
    }
    
    trained_ = true;
}

void NeuralNetworkCostModel::train_module(
    const std::vector<std::pair<ModuleFeatures, double>>& data) {
    
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    
    for (const auto& p : data) {
        X.push_back(p.first.to_feature_vector());
        y.push_back(p.second);
    }
    
    train({});
}

void NeuralNetworkCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "neural_network\n";
    out << trained_ << "\n";
    out << layer_sizes_.size() << "\n";
    for (int s : layer_sizes_) out << s << " ";
    out << "\n";
    for (const auto& w : weights_) {
        out << w.size() << " ";
        for (double v : w) out << v << " ";
    }
    out << "\n";
    for (const auto& b : biases_) {
        out << b.size() << " ";
        for (double v : b) out << v << " ";
    }
    out << "\n";
}

void NeuralNetworkCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    in >> trained_;
    size_t n_layers;
    in >> n_layers;
    layer_sizes_.resize(n_layers);
    for (size_t i = 0; i < n_layers; ++i) in >> layer_sizes_[i];
    weights_.clear();
    biases_.clear();
    for (size_t i = 0; i < n_layers - 1; ++i) {
        size_t wsize;
        in >> wsize;
        weights_.push_back(std::vector<double>(wsize));
        for (size_t j = 0; j < wsize; ++j) in >> weights_.back()[j];
    }
    for (size_t i = 0; i < n_layers - 1; ++i) {
        size_t bsize;
        in >> bsize;
        biases_.push_back(std::vector<double>(bsize));
        for (size_t j = 0; j < bsize; ++j) in >> biases_.back()[j];
    }
}

std::string NeuralNetworkCostModel::model_info() const {
    std::ostringstream oss;
    oss << "NeuralNetwork[";
    for (size_t i = 0; i < layer_sizes_.size(); ++i) {
        if (i > 0) oss << "->";
        oss << layer_sizes_[i];
    }
    oss << ",trained=" << trained_ << "]";
    return oss.str();
}

// ============================================================================
// EnsembleCostModel Implementation
// ============================================================================

EnsembleCostModel::EnsembleCostModel() {}

double EnsembleCostModel::predict(const OpFeatures& features) {
    if (models_.empty()) {
        return features.flop_count / 1e6 + 0.1;
    }
    
    double total_weight = 0;
    double weighted_sum = 0;
    
    for (size_t i = 0; i < models_.size(); ++i) {
        double pred = models_[i]->predict(features);
        if (!std::isnan(pred) && pred > 0) {
            weighted_sum += weights_[i] * pred;
            total_weight += weights_[i];
        }
    }
    
    if (total_weight <= 0) {
        return features.flop_count / 1e6 + 0.1;
    }
    return weighted_sum / total_weight;
}

double EnsembleCostModel::predict_module(const ModuleFeatures& features) {
    if (models_.empty()) {
        return features.total_flops / 1e6 + 0.1;
    }
    
    double total_weight = 0;
    double weighted_sum = 0;
    
    for (size_t i = 0; i < models_.size(); ++i) {
        double pred = models_[i]->predict_module(features);
        if (!std::isnan(pred) && pred > 0) {
            weighted_sum += weights_[i] * pred;
            total_weight += weights_[i];
        }
    }
    
    if (total_weight <= 0) {
        return features.total_flops / 1e6 + 0.1;
    }
    return weighted_sum / total_weight;
}

void EnsembleCostModel::train(
    const std::vector<std::pair<OpFeatures, double>>& data) {
    for (auto& model : models_) {
        model->train(data);
    }
}

void EnsembleCostModel::train_module(
    const std::vector<std::pair<ModuleFeatures, double>>& data) {
    for (auto& model : models_) {
        model->train_module(data);
    }
}

void EnsembleCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "ensemble\n";
    out << models_.size() << "\n";
    for (size_t i = 0; i < models_.size(); ++i) {
        out << weights_[i] << " ";
        models_[i]->save(path + "." + std::to_string(i));
    }
}

void EnsembleCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    size_t n;
    in >> n;
    weights_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        in >> weights_[i];
    }
}

std::string EnsembleCostModel::model_info() const {
    std::ostringstream oss;
    oss << "Ensemble[models=" << models_.size() << ",trained=" << is_trained() << "]";
    return oss.str();
}

bool EnsembleCostModel::is_trained() const {
    return all_trained_();
}

bool EnsembleCostModel::all_trained_() const {
    for (const auto& m : models_) {
        if (!m->is_trained()) return false;
    }
    return !models_.empty();
}

void EnsembleCostModel::add_model(std::unique_ptr<CostModel> model, double weight) {
    models_.push_back(std::move(model));
    weights_.push_back(weight);
}

void EnsembleCostModel::set_weight(int index, double weight) {
    if (index >= 0 && index < static_cast<int>(weights_.size())) {
        weights_[index] = weight;
    }
}

// ============================================================================
// Cost Model Factory
// ============================================================================

std::unique_ptr<CostModel> create_cost_model(CostModelType type) {
    switch (type) {
        case CostModelType::HEURISTIC:
            return std::make_unique<HeuristicCostModel>();
        case CostModelType::LINEAR_REGRESSION:
            return std::make_unique<LinearRegressionCostModel>();
        case CostModelType::XGBOOST:
            return std::make_unique<XGBoostCostModel>();
        case CostModelType::NEURAL_NETWORK:
            return std::make_unique<NeuralNetworkCostModel>();
        case CostModelType::ENSEMBLE: {
            auto ensemble = std::make_unique<EnsembleCostModel>();
            ensemble->add_model(std::make_unique<HeuristicCostModel>(), 0.2);
            ensemble->add_model(std::make_unique<LinearRegressionCostModel>(), 0.3);
            ensemble->add_model(std::make_unique<XGBoostCostModel>(), 0.5);
            return ensemble;
        }
        default:
            return std::make_unique<HeuristicCostModel>();
    }
}

std::unique_ptr<CostModel> create_cost_model(const std::string& type_name) {
    if (type_name == "heuristic") return create_cost_model(CostModelType::HEURISTIC);
    if (type_name == "linear" || type_name == "linear_regression") 
        return create_cost_model(CostModelType::LINEAR_REGRESSION);
    if (type_name == "xgboost" || type_name == "xgb") 
        return create_cost_model(CostModelType::XGBOOST);
    if (type_name == "neural" || type_name == "nn" || type_name == "neural_network")
        return create_cost_model(CostModelType::NEURAL_NETWORK);
    if (type_name == "ensemble")
        return create_cost_model(CostModelType::ENSEMBLE);
    return create_cost_model(CostModelType::HEURISTIC);
}

// ============================================================================
// ProfileGuidedCostModel Implementation
// ============================================================================

ProfileGuidedCostModel::ProfileGuidedCostModel() {
    fallback_model_ = std::make_unique<HeuristicCostModel>();
}

double ProfileGuidedCostModel::predict(const OpFeatures& features) {
    if (profile_data_.empty()) {
        return fallback_model_->predict(features);
    }
    
    // 查找相似的 profile 数据
    auto similar = find_similar(features, 5);
    if (similar.empty()) {
        return fallback_model_->predict(features);
    }
    
    // 加权平均
    double total_weight = 0;
    double weighted_sum = 0;
    for (const auto& p : similar) {
        double sim = similarity(features, p.features);
        weighted_sum += sim * p.actual_time_ms;
        total_weight += sim;
    }
    
    return total_weight > 0 ? weighted_sum / total_weight : fallback_model_->predict(features);
}

double ProfileGuidedCostModel::predict_module(const ModuleFeatures& features) {
    if (profile_data_.empty()) {
        return fallback_model_->predict_module(features);
    }
    return fallback_model_->predict_module(features);
}

void ProfileGuidedCostModel::train(
    const std::vector<std::pair<OpFeatures, double>>& data) {
    for (const auto& p : data) {
        ProfileDataPoint pd;
        pd.features = p.first;
        pd.actual_time_ms = p.second;
        profile_data_.push_back(pd);
    }
}

void ProfileGuidedCostModel::train_module(
    const std::vector<std::pair<ModuleFeatures, double>>& data) {
    (void)data;
}

void ProfileGuidedCostModel::save(const std::string& path) {
    std::ofstream out(path);
    out << "profile_guided\n";
    out << profile_data_.size() << "\n";
    for (const auto& p : profile_data_) {
        out << p.actual_time_ms << " ";
        out << static_cast<int>(p.hardware) << " ";
    }
    out << "\n";
}

void ProfileGuidedCostModel::load(const std::string& path) {
    std::ifstream in(path);
    std::string type;
    in >> type;
    size_t n;
    in >> n;
    profile_data_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        in >> profile_data_[i].actual_time_ms;
        int hw;
        in >> hw;
        profile_data_[i].hardware = static_cast<HardwareTarget>(hw);
    }
}

std::string ProfileGuidedCostModel::model_info() const {
    std::ostringstream oss;
    oss << "ProfileGuided[data=" << profile_data_.size() << "]";
    return oss.str();
}

bool ProfileGuidedCostModel::is_trained() const {
    return !profile_data_.empty();
}

void ProfileGuidedCostModel::add_profile_data(const ProfileDataPoint& data) {
    profile_data_.push_back(data);
}

std::vector<ProfileDataPoint> ProfileGuidedCostModel::find_similar(
    const OpFeatures& features, int top_k) const {
    
    std::vector<std::pair<double, size_t>> scores;
    for (size_t i = 0; i < profile_data_.size(); ++i) {
        scores.push_back({similarity(features, profile_data_[i].features), i});
    }
    
    std::sort(scores.begin(), scores.end(), 
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<ProfileDataPoint> result;
    for (int i = 0; i < top_k && i < static_cast<int>(scores.size()); ++i) {
        result.push_back(profile_data_[scores[i].second]);
    }
    
    return result;
}

double ProfileGuidedCostModel::similarity(
    const OpFeatures& a, const OpFeatures& b) const {
    
    if (a.op_kind != b.op_kind) return 0;
    
    double shape_sim = 0;
    if (!a.output_shapes.empty() && !b.output_shapes.empty()) {
        size_t n = std::min(a.output_shapes.size(), b.output_shapes.size());
        for (size_t i = 0; i < n; ++i) {
            if (a.output_shapes[i] == b.output_shapes[i]) shape_sim += 1;
        }
        shape_sim /= n;
    }
    
    double feature_sim = 0;
    auto fv_a = a.to_feature_vector();
    auto fv_b = b.to_feature_vector();
    size_t n = std::min(fv_a.size(), fv_b.size());
    for (size_t i = 0; i < n; ++i) {
        feature_sim += 1.0 / (1.0 + std::abs(fv_a[i] - fv_b[i]));
    }
    feature_sim /= n;
    
    return (shape_sim + feature_sim) / 2;
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string op_kind_to_string(OpKind kind) {
    switch (kind) {
        case OpKind::ADD: return "ADD";
        case OpKind::SUB: return "SUB";
        case OpKind::MUL: return "MUL";
        case OpKind::DIV: return "DIV";
        case OpKind::MOD: return "MOD";
        case OpKind::SUM: return "SUM";
        case OpKind::MEAN: return "MEAN";
        case OpKind::MAX: return "MAX";
        case OpKind::MIN: return "MIN";
        case OpKind::ARGMAX: return "ARGMAX";
        case OpKind::ARGMIN: return "ARGMIN";
        case OpKind::MATMUL: return "MATMUL";
        case OpKind::TENSORDOT: return "TENSORDOT";
        case OpKind::TRANSPOSE: return "TRANSPOSE";
        case OpKind::RESHAPE: return "RESHAPE";
        case OpKind::SLICE: return "SLICE";
        case OpKind::CONCAT: return "CONCAT";
        case OpKind::SPLIT: return "SPLIT";
        case OpKind::CONV2D: return "CONV2D";
        case OpKind::CONV3D: return "CONV3D";
        case OpKind::MAXPOOL: return "MAXPOOL";
        case OpKind::AVGPOOL: return "AVGPOOL";
        case OpKind::RELU: return "RELU";
        case OpKind::SIGMOID: return "SIGMOID";
        case OpKind::TANH: return "TANH";
        case OpKind::SOFTMAX: return "SOFTMAX";
        case OpKind::LOAD: return "LOAD";
        case OpKind::STORE: return "STORE";
        case OpKind::ALLOC: return "ALLOC";
        case OpKind::COPY: return "COPY";
        case OpKind::LOOP: return "LOOP";
        case OpKind::CONDITION: return "CONDITION";
        case OpKind::CALL: return "CALL";
        default: return "UNKNOWN";
    }
}

OpKind string_to_op_kind(const std::string& s) {
    static const std::unordered_map<std::string, OpKind> map = {
        {"ADD", OpKind::ADD}, {"SUB", OpKind::SUB},
        {"MUL", OpKind::MUL}, {"DIV", OpKind::DIV}, {"MOD", OpKind::MOD},
        {"SUM", OpKind::SUM}, {"MEAN", OpKind::MEAN},
        {"MAX", OpKind::MAX}, {"MIN", OpKind::MIN},
        {"ARGMAX", OpKind::ARGMAX}, {"ARGMIN", OpKind::ARGMIN},
        {"MATMUL", OpKind::MATMUL}, {"TENSORDOT", OpKind::TENSORDOT},
        {"TRANSPOSE", OpKind::TRANSPOSE}, {"RESHAPE", OpKind::RESHAPE},
        {"SLICE", OpKind::SLICE}, {"CONCAT", OpKind::CONCAT}, {"SPLIT", OpKind::SPLIT},
        {"CONV2D", OpKind::CONV2D}, {"CONV3D", OpKind::CONV3D},
        {"MAXPOOL", OpKind::MAXPOOL}, {"AVGPOOL", OpKind::AVGPOOL},
        {"RELU", OpKind::RELU}, {"SIGMOID", OpKind::SIGMOID},
        {"TANH", OpKind::TANH}, {"SOFTMAX", OpKind::SOFTMAX},
        {"LOAD", OpKind::LOAD}, {"STORE", OpKind::STORE},
        {"ALLOC", OpKind::ALLOC}, {"COPY", OpKind::COPY},
        {"LOOP", OpKind::LOOP}, {"CONDITION", OpKind::CONDITION},
        {"CALL", OpKind::CALL}
    };
    auto it = map.find(s);
    return it != map.end() ? it->second : OpKind::UNKNOWN;
}

std::string layout_to_string(MemoryLayout layout) {
    switch (layout) {
        case MemoryLayout::ROW_MAJOR: return "ROW_MAJOR";
        case MemoryLayout::COL_MAJOR: return "COL_MAJOR";
        case MemoryLayout::NHWC: return "NHWC";
        case MemoryLayout::NCHW: return "NCHW";
        case MemoryLayout::CHWN: return "CHWN";
        default: return "UNKNOWN";
    }
}

std::string hardware_to_string(HardwareTarget hw) {
    switch (hw) {
        case HardwareTarget::CPU: return "CPU";
        case HardwareTarget::GPU_NVIDIA: return "GPU_NVIDIA";
        case HardwareTarget::GPU_AMD: return "GPU_AMD";
        case HardwareTarget::TPU: return "TPU";
        case HardwareTarget::CPU_SIMD: return "CPU_SIMD";
        case HardwareTarget::CPU_MULTI_CORE: return "CPU_MULTI_CORE";
        default: return "UNKNOWN";
    }
}

void print_features(const OpFeatures& features) {
    std::cout << "OpFeatures: " << features.op_name << "\n";
    std::cout << "  Kind: " << op_kind_to_string(features.op_kind) << "\n";
    std::cout << "  Inputs/Outputs: " << features.num_inputs << "/" << features.num_outputs << "\n";
    std::cout << "  Dims: " << features.num_dims << ", Elements: " << features.total_elements << "\n";
    std::cout << "  FLOPs: " << features.flop_count << ", Memory: " << features.memory_bytes << "\n";
    std::cout << "  Arithmetic Intensity: " << features.arithmetic_intensity << "\n";
    std::cout << "  Has Reduction: " << features.has_reduction << "\n";
    std::cout << "  Has Broadcast: " << features.has_broadcast << "\n";
}

void print_module_features(const ModuleFeatures& features) {
    std::cout << "ModuleFeatures:\n";
    std::cout << "  Total Ops: " << features.total_ops << "\n";
    std::cout << "  Total FLOPs: " << features.total_flops << "\n";
    std::cout << "  Total Memory: " << features.total_memory_bytes << "\n";
    std::cout << "  Avg Arithmetic Intensity: " << features.avg_arithmetic_intensity << "\n";
    std::cout << "  Max Loop Nest: " << features.max_loop_nest_depth << "\n";
    std::cout << "  DAG Depth: " << features.dag_depth << "\n";
    std::cout << "  Parallelism Ratio: " << features.parallelism_ratio << "\n";
}

} // namespace ml
} // namespace claw
