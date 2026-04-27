// auto_scheduler/schedule_space.cpp - 调度搜索空间实现

#include "schedule_space.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <set>

namespace claw {
namespace scheduler {
using namespace tensorir;

// ============================================================================
// ScheduleDecision
// ============================================================================

ScheduleDecision ScheduleDecision::tile(const std::vector<std::string>& axes,
                                         const std::vector<int64_t>& sizes) {
    ScheduleDecision dec(Kind::Tile);
    dec.str_params = axes;
    dec.int_params = sizes;
    return dec;
}

ScheduleDecision ScheduleDecision::fuse(const std::string& a, const std::string& b) {
    ScheduleDecision dec(Kind::Fuse);
    dec.str_params = {a, b};
    return dec;
}

ScheduleDecision ScheduleDecision::split(const std::string& iter, int64_t factor) {
    ScheduleDecision dec(Kind::Split);
    dec.str_params = {iter};
    dec.int_params = {factor};
    return dec;
}

ScheduleDecision ScheduleDecision::reorder(const std::vector<std::string>& order) {
    ScheduleDecision dec(Kind::Reorder);
    dec.str_params = order;
    return dec;
}

ScheduleDecision ScheduleDecision::vectorize(const std::string& iter, int64_t len) {
    ScheduleDecision dec(Kind::Vectorize);
    dec.str_params = {iter};
    dec.int_params = {len};
    return dec;
}

ScheduleDecision ScheduleDecision::unroll(const std::string& iter, int64_t factor) {
    ScheduleDecision dec(Kind::Unroll);
    dec.str_params = {iter};
    dec.int_params = {factor};
    return dec;
}

ScheduleDecision ScheduleDecision::parallel(const std::string& iter, int64_t threads) {
    ScheduleDecision dec(Kind::Parallel);
    dec.str_params = {iter};
    dec.int_params = {threads};
    return dec;
}

ScheduleDecision ScheduleDecision::cache_read(const std::string& buf, const std::string& scope) {
    ScheduleDecision dec(Kind::CacheRead);
    dec.str_params = {buf, scope};
    return dec;
}

ScheduleDecision ScheduleDecision::cache_write(const std::string& buf, const std::string& scope) {
    ScheduleDecision dec(Kind::CacheWrite);
    dec.str_params = {buf, scope};
    return dec;
}

ScheduleDecision ScheduleDecision::compute_at(const std::string& producer,
                                               const std::string& consumer,
                                               const std::string& iter) {
    ScheduleDecision dec(Kind::ComputeAt);
    dec.str_params = {producer, consumer, iter};
    return dec;
}

ScheduleDecision ScheduleDecision::inline_op(const std::string& op) {
    ScheduleDecision dec(Kind::Inline);
    dec.str_params = {op};
    return dec;
}

std::string ScheduleDecision::to_string() const {
    std::ostringstream oss;
    switch (kind) {
        case Kind::Tile: 
            oss << "tile(";
            for (size_t i = 0; i < str_params.size(); i++) {
                if (i > 0) oss << ", ";
                oss << str_params[i];
            }
            oss << " | ";
            for (size_t i = 0; i < int_params.size(); i++) {
                if (i > 0) oss << ", ";
                oss << int_params[i];
            }
            oss << ")";
            break;
        case Kind::Fuse: 
            oss << "fuse(" << str_params[0] << ", " << str_params[1] << ")";
            break;
        case Kind::Split: 
            oss << "split(" << str_params[0] << ", " << int_params[0] << ")";
            break;
        case Kind::Reorder: 
            oss << "reorder(";
            for (size_t i = 0; i < str_params.size(); i++) {
                if (i > 0) oss << ", ";
                oss << str_params[i];
            }
            oss << ")";
            break;
        case Kind::Vectorize: 
            oss << "vectorize(" << str_params[0] << ", " << int_params[0] << ")";
            break;
        case Kind::Unroll: 
            oss << "unroll(" << str_params[0] << ", " << int_params[0] << ")";
            break;
        case Kind::Parallel: 
            oss << "parallel(" << str_params[0] << ", " << int_params[0] << ")";
            break;
        case Kind::CacheRead: 
            oss << "cache_read(" << str_params[0] << ", " << str_params[1] << ")";
            break;
        case Kind::CacheWrite: 
            oss << "cache_write(" << str_params[0] << ", " << str_params[1] << ")";
            break;
        case Kind::ComputeAt: 
            oss << "compute_at(" << str_params[0] << ", " << str_params[1] 
                << ", " << str_params[2] << ")";
            break;
        case Kind::Inline: 
            oss << "inline(" << str_params[0] << ")";
            break;
    }
    return oss.str();
}

// ============================================================================
// ScheduleConfig
// ============================================================================

bool ScheduleConfig::apply(Schedule& sched) const {
    for (const auto& dec : decisions) {
        try {
            switch (dec.kind) {
                case ScheduleDecision::Kind::Tile:
                    if (dec.str_params.size() >= 2 && dec.int_params.size() >= 2) {
                        sched.tile(dec.str_params, dec.int_params);
                    }
                    break;
                case ScheduleDecision::Kind::Fuse:
                    if (dec.str_params.size() >= 2) {
                        sched.fuse(dec.str_params[0], dec.str_params[1], 
                                   dec.str_params[0] + "_" + dec.str_params[1]);
                    }
                    break;
                case ScheduleDecision::Kind::Split:
                    if (!dec.str_params.empty() && !dec.int_params.empty()) {
                        sched.split(dec.str_params[0], dec.int_params[0]);
                    }
                    break;
                case ScheduleDecision::Kind::Reorder:
                    if (!dec.str_params.empty()) {
                        sched.reorder(dec.str_params);
                    }
                    break;
                case ScheduleDecision::Kind::Vectorize:
                    if (!dec.str_params.empty()) {
                        sched.vectorize(dec.str_params[0]);
                    }
                    break;
                case ScheduleDecision::Kind::Unroll:
                    if (!dec.str_params.empty()) {
                        sched.unroll(dec.str_params[0]);
                    }
                    break;
                case ScheduleDecision::Kind::Parallel:
                    if (!dec.str_params.empty()) {
                        sched.parallel(dec.str_params[0]);
                    }
                    break;
                case ScheduleDecision::Kind::Bind:
                    if (dec.str_params.size() >= 2) {
                        sched.bind(dec.str_params[0], dec.str_params[1]);
                    }
                    break;
                case ScheduleDecision::Kind::CacheRead:
                    if (dec.str_params.size() >= 2) {
                        sched.cache_read(dec.str_params[0], dec.str_params[1]);
                    }
                    break;
                case ScheduleDecision::Kind::CacheWrite:
                    if (dec.str_params.size() >= 2) {
                        sched.cache_write(dec.str_params[0], dec.str_params[1]);
                    }
                    break;
                case ScheduleDecision::Kind::ComputeAt:
                    // 需要额外处理，暂不实现
                    break;
                case ScheduleDecision::Kind::Inline:
                    sched.inline_op();
                    break;
            }
        } catch (...) {
            return false;
        }
    }
    return true;
}

std::string ScheduleConfig::signature() const {
    std::ostringstream oss;
    for (const auto& dec : decisions) {
        oss << static_cast<int>(dec.kind) << ":";
        for (const auto& s : dec.str_params) oss << s << ",";
        oss << "|";
        for (const auto& i : dec.int_params) oss << i << ",";
        oss << ";";
    }
    return oss.str();
}

std::string ScheduleConfig::to_string() const {
    std::ostringstream oss;
    oss << "ScheduleConfig[" << decisions.size() << " decisions]\n";
    for (size_t i = 0; i < decisions.size(); i++) {
        oss << "  [" << i << "] " << decisions[i].to_string() << "\n";
    }
    if (is_measured) {
        oss << "  measured: " << measured_time << " ms\n";
    } else if (estimated_score > 0) {
        oss << "  estimated: " << estimated_score << "\n";
    }
    return oss.str();
}

// ============================================================================
// ParamDomain
// ============================================================================

ParamDomain ParamDomain::fixed(std::initializer_list<int64_t> vals) {
    ParamDomain d;
    d.type = Type::FixedList;
    d.values = vals;
    return d;
}

ParamDomain ParamDomain::range(int64_t min_v, int64_t max_v, int64_t step_v) {
    ParamDomain d;
    d.type = Type::Range;
    d.min_val = min_v;
    d.max_val = max_v;
    d.step = step_v;
    return d;
}

ParamDomain ParamDomain::power_of_two(int64_t min_e, int64_t max_e) {
    ParamDomain d;
    d.type = Type::PowerOfTwo;
    d.min_exp = min_e;
    d.max_exp = max_e;
    return d;
}

ParamDomain ParamDomain::divisors(int64_t max_n) {
    ParamDomain d;
    d.type = Type::Divisor;
    d.max_val = max_n;
    return d;
}

std::vector<int64_t> ParamDomain::enumerate() const {
    std::vector<int64_t> result;
    switch (type) {
        case Type::FixedList:
            return values;
        case Type::Range:
            for (int64_t v = min_val; v <= max_val; v += step) {
                result.push_back(v);
            }
            return result;
        case Type::PowerOfTwo:
            for (int64_t e = min_exp; e <= max_exp; e++) {
                result.push_back(1LL << e);
            }
            return result;
        case Type::Divisor:
            for (int64_t i = 1; i <= max_val; i++) {
                if (max_val % i == 0) result.push_back(i);
            }
            return result;
    }
    return result;
}

int64_t ParamDomain::random_sample(std::mt19937& rng) const {
    auto vals = enumerate();
    if (vals.empty()) return 1;
    std::uniform_int_distribution<size_t> dist(0, vals.size() - 1);
    return vals[dist(rng)];
}

// ============================================================================
// ScheduleSpace
// ============================================================================

ScheduleSpace::ScheduleSpace(TensorOp* op, const TensorIRModule* module) 
    : op_(op), module_(module) {
    build_rules();
}

void ScheduleSpace::build_rules() {
    add_tile_rules();
    add_split_rules();
    add_fuse_rules();
    add_parallel_rules();
    add_vectorize_rules();
    add_unroll_rules();
    add_cache_rules();
    add_compute_at_rules();
}

void ScheduleSpace::add_tile_rules() {
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    if (iters.size() >= 2) {
        // 2D tile: 最常见的模式
        TransformRule rule(ScheduleDecision::Kind::Tile);
        rule.applicable_axes = iters;
        // Tile sizes: 通常是 2 的幂或 8/16/32/64/128/256
        rule.param_domains.push_back(ParamDomain::power_of_two(3, 8)); // 8~256
        rule.param_domains.push_back(ParamDomain::power_of_two(3, 8));
        rule.weight = 2.0;  // 高权重
        rules_.push_back(rule);
    }
    
    // 1D tile for each axis
    for (size_t i = 0; i < iters.size() && i < extents.size(); i++) {
        if (extents[i] >= 16) {
            TransformRule rule(ScheduleDecision::Kind::Tile);
            rule.applicable_axes = {iters[i]};
            rule.param_domains.push_back(ParamDomain::power_of_two(3, 8));
            rule.weight = 1.0;
            rules_.push_back(rule);
        }
    }
}

void ScheduleSpace::add_split_rules() {
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    for (size_t i = 0; i < iters.size() && i < extents.size(); i++) {
        if (extents[i] >= 32) {
            TransformRule rule(ScheduleDecision::Kind::Split);
            rule.applicable_axes = {iters[i]};
            // Split factor: 常用 2/4/8/16/32/64/128/256/512/1024
            rule.param_domains.push_back(ParamDomain::power_of_two(1, 10));
            rule.weight = 1.5;
            rules_.push_back(rule);
        }
    }
}

void ScheduleSpace::add_fuse_rules() {
    auto pairs = get_fusible_pairs();
    for (const auto& p : pairs) {
        TransformRule rule(ScheduleDecision::Kind::Fuse);
        rule.applicable_axes = {p.first, p.second};
        rule.weight = 0.8;
        rules_.push_back(rule);
    }
}

void ScheduleSpace::add_parallel_rules() {
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    for (size_t i = 0; i < iters.size() && i < extents.size(); i++) {
        if (extents[i] >= 4) {
            TransformRule rule(ScheduleDecision::Kind::Parallel);
            rule.applicable_axes = {iters[i]};
            rule.param_domains.push_back(ParamDomain::fixed({2, 4, 8, 16, 32, 64}));
            rule.weight = 1.2;
            rules_.push_back(rule);
        }
    }
}

void ScheduleSpace::add_vectorize_rules() {
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    for (size_t i = 0; i < iters.size() && i < extents.size(); i++) {
        if (extents[i] >= 4) {
            TransformRule rule(ScheduleDecision::Kind::Vectorize);
            rule.applicable_axes = {iters[i]};
            rule.param_domains.push_back(ParamDomain::fixed({2, 4, 8, 16}));
            rule.weight = 1.3;
            rules_.push_back(rule);
        }
    }
}

void ScheduleSpace::add_unroll_rules() {
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    for (size_t i = 0; i < iters.size() && i < extents.size(); i++) {
        if (extents[i] >= 2 && extents[i] <= 256) {
            TransformRule rule(ScheduleDecision::Kind::Unroll);
            rule.applicable_axes = {iters[i]};
            // Unroll factor
            rule.param_domains.push_back(ParamDomain::fixed({2, 4, 8, 16}));
            rule.weight = 0.7;
            rules_.push_back(rule);
        }
    }
}

void ScheduleSpace::add_cache_rules() {
    if (!op_->inputs.empty()) {
        TransformRule rule(ScheduleDecision::Kind::CacheRead);
        rule.weight = 0.5;
        rules_.push_back(rule);
    }
    if (!op_->outputs.empty()) {
        TransformRule rule(ScheduleDecision::Kind::CacheWrite);
        rule.weight = 0.5;
        rules_.push_back(rule);
    }
}

void ScheduleSpace::add_compute_at_rules() {
    // 查找可以 compute_at 的消费者
    if (module_) {
        for (const auto& other_op : module_->operations) {
            if (other_op.get() != op_) {
                TransformRule rule(ScheduleDecision::Kind::ComputeAt);
                rule.weight = 0.3;
                rules_.push_back(rule);
            }
        }
    }
}

std::vector<std::string> ScheduleSpace::get_iter_names() const {
    std::vector<std::string> names;
    for (const auto* iv : op_->iter_vars) {
        if (iv) names.push_back(iv->name);
    }
    return names;
}

std::vector<int64_t> ScheduleSpace::get_iter_extents() const {
    std::vector<int64_t> extents;
    for (const auto* iv : op_->iter_vars) {
        if (iv && std::holds_alternative<int64_t>(iv->range.extent)) {
            extents.push_back(std::get<int64_t>(iv->range.extent));
        } else {
            extents.push_back(1);
        }
    }
    return extents;
}

ScheduleSpace::OpFeatures ScheduleSpace::get_features() const {
    OpFeatures f;
    
    switch (op_->kind) {
        case TensorOp::OpKind::Compute: f.op_kind = "compute"; break;
        case TensorOp::OpKind::Matmul: f.op_kind = "matmul"; break;
        case TensorOp::OpKind::Conv2d: f.op_kind = "conv2d"; break;
        case TensorOp::OpKind::Pool2d: f.op_kind = "pool2d"; break;
        case TensorOp::OpKind::Reduce: f.op_kind = "reduce"; break;
        case TensorOp::OpKind::Cast: f.op_kind = "cast"; break;
        case TensorOp::OpKind::Broadcast: f.op_kind = "broadcast"; break;
        case TensorOp::OpKind::Slice: f.op_kind = "slice"; break;
    }
    
    f.num_inputs = op_->inputs.size();
    f.num_outputs = op_->outputs.size();
    f.num_dims = op_->iter_vars.size();
    
    // 计算输出形状和算术强度
    int64_t total_flops = 1;
    int64_t total_bytes = 0;
    
    for (const auto* out : op_->outputs) {
        if (out) {
            int64_t size = 1;
            for (const auto& dim : out->shape) {
                if (std::holds_alternative<int64_t>(dim)) {
                    size *= std::get<int64_t>(dim);
                }
            }
            f.output_shape.push_back(size);
            total_bytes += size * 4; // 假设 f32
        }
    }
    
    for (const auto* in : op_->inputs) {
        if (in) {
            int64_t size = 1;
            for (const auto& dim : in->shape) {
                if (std::holds_alternative<int64_t>(dim)) {
                    size *= std::get<int64_t>(dim);
                }
            }
            total_bytes += size * 4;
        }
    }
    
    // 估算 FLOPs
    if (op_->kind == TensorOp::OpKind::Matmul) {
        auto extents = get_iter_extents();
        if (extents.size() >= 3) {
            total_flops = extents[0] * extents[1] * extents[2] * 2; // M*N*K*2
        }
    } else {
        auto extents = get_iter_extents();
        total_flops = std::accumulate(extents.begin(), extents.end(), 
                                       1LL, std::multiplies<int64_t>());
    }
    
    f.arithmetic_intensity = total_bytes > 0 ? total_flops / total_bytes : 0;
    
    // 检查是否包含归约维度
    f.is_reduction = false;
    for (const auto* iv : op_->iter_vars) {
        if (iv && iv->kind == IterVar::VarKind::Reduc) {
            f.is_reduction = true;
            break;
        }
    }
    
    return f;
}

ScheduleConfig ScheduleSpace::random_sample(std::mt19937& rng) const {
    ScheduleConfig config;
    std::uniform_real_distribution<double> weight_dist(0.0, 1.0);
    
    // 根据权重随机选择规则并采样
    for (const auto& rule : rules_) {
        if (weight_dist(rng) > rule.weight / 3.0) continue;  // 按权重概率跳过
        
        ScheduleDecision dec(rule.kind);
        
        switch (rule.kind) {
            case ScheduleDecision::Kind::Tile: {
                if (rule.applicable_axes.size() >= 2) {
                    std::vector<std::string> axes;
                    std::vector<int64_t> sizes;
                    for (size_t i = 0; i < 2 && i < rule.applicable_axes.size(); i++) {
                        axes.push_back(rule.applicable_axes[i]);
                        if (i < rule.param_domains.size()) {
                            sizes.push_back(rule.param_domains[i].random_sample(rng));
                        } else {
                            sizes.push_back(32);
                        }
                    }
                    dec = ScheduleDecision::tile(axes, sizes);
                }
                break;
            }
            case ScheduleDecision::Kind::Split: {
                if (!rule.applicable_axes.empty() && !rule.param_domains.empty()) {
                    dec = ScheduleDecision::split(rule.applicable_axes[0], 
                                                   rule.param_domains[0].random_sample(rng));
                }
                break;
            }
            case ScheduleDecision::Kind::Fuse: {
                if (rule.applicable_axes.size() >= 2) {
                    dec = ScheduleDecision::fuse(rule.applicable_axes[0], rule.applicable_axes[1]);
                }
                break;
            }
            case ScheduleDecision::Kind::Vectorize: {
                if (!rule.applicable_axes.empty() && !rule.param_domains.empty()) {
                    dec = ScheduleDecision::vectorize(rule.applicable_axes[0],
                                                       rule.param_domains[0].random_sample(rng));
                }
                break;
            }
            case ScheduleDecision::Kind::Unroll: {
                if (!rule.applicable_axes.empty() && !rule.param_domains.empty()) {
                    dec = ScheduleDecision::unroll(rule.applicable_axes[0],
                                                    rule.param_domains[0].random_sample(rng));
                }
                break;
            }
            case ScheduleDecision::Kind::Parallel: {
                if (!rule.applicable_axes.empty() && !rule.param_domains.empty()) {
                    dec = ScheduleDecision::parallel(rule.applicable_axes[0],
                                                      rule.param_domains[0].random_sample(rng));
                }
                break;
            }
            case ScheduleDecision::Kind::CacheRead: {
                if (!op_->inputs.empty() && op_->inputs[0]) {
                    dec = ScheduleDecision::cache_read(op_->inputs[0]->name, "local");
                }
                break;
            }
            case ScheduleDecision::Kind::CacheWrite: {
                if (!op_->outputs.empty() && op_->outputs[0]) {
                    dec = ScheduleDecision::cache_write(op_->outputs[0]->name, "local");
                }
                break;
            }
            default:
                continue;
        }
        
        if (!dec.str_params.empty()) {
            config.add(dec);
        }
    }
    
    return config;
}

ScheduleConfig ScheduleSpace::random_sample_greedy(std::mt19937& rng, int max_depth) const {
    ScheduleConfig config;
    std::vector<bool> used(rules_.size(), false);
    
    for (int d = 0; d < max_depth; d++) {
        // 找到未使用的规则中权重最高的
        size_t best_idx = rules_.size();
        double best_weight = -1.0;
        
        for (size_t i = 0; i < rules_.size(); i++) {
            if (!used[i] && rules_[i].weight > best_weight) {
                best_weight = rules_[i].weight;
                best_idx = i;
            }
        }
        
        if (best_idx >= rules_.size()) break;
        used[best_idx] = true;
        
        // 采样该规则
        const auto& rule = rules_[best_idx];
        ScheduleDecision dec(rule.kind);
        
        // 简化处理：复用 random_sample 的逻辑，但只取一条
        // ... (省略详细实现，使用简化的通用逻辑)
    }
    
    return config;
}

size_t ScheduleSpace::estimated_size() const {
    size_t total = 1;
    for (const auto& rule : rules_) {
        size_t rule_options = 1;
        for (const auto& domain : rule.param_domains) {
            rule_options *= std::max(size_t(1), domain.enumerate().size());
        }
        total *= (rule_options + 1);  // +1 for "not applied"
    }
    return total;
}

bool ScheduleSpace::is_valid(const ScheduleConfig& config) const {
    // 基本合法性检查
    if (config.decisions.empty()) return true;
    
    std::set<std::string> used_iters;
    for (const auto& dec : config.decisions) {
        for (const auto& axis : dec.str_params) {
            if (used_iters.count(axis) && 
                (dec.kind == ScheduleDecision::Kind::Vectorize ||
                 dec.kind == ScheduleDecision::Kind::Parallel ||
                 dec.kind == ScheduleDecision::Kind::Unroll)) {
                // 同一迭代变量不能同时应用多种变换
                return false;
            }
            used_iters.insert(axis);
        }
    }
    return true;
}

ScheduleConfig ScheduleSpace::get_default_config() const {
    ScheduleConfig config;
    auto iters = get_iter_names();
    auto extents = get_iter_extents();
    
    // 默认策略：对最外层并行，对内层向量化
    if (!iters.empty() && !extents.empty()) {
        // 并行最外层
        if (extents[0] >= 4) {
            config.add(ScheduleDecision::parallel(iters[0], 4));
        }
        
        // 向量化最内层（如果维度足够）
        if (iters.size() > 1 && extents.back() >= 4) {
            config.add(ScheduleDecision::vectorize(iters.back(), 4));
        }
    }
    
    return config;
}

std::vector<std::pair<std::string, std::string>> ScheduleSpace::get_fusible_pairs() const {
    std::vector<std::pair<std::string, std::string>> pairs;
    auto iters = get_iter_names();
    
    for (size_t i = 0; i + 1 < iters.size(); i++) {
        pairs.emplace_back(iters[i], iters[i+1]);
    }
    return pairs;
}

std::vector<int64_t> ScheduleSpace::get_legal_tile_sizes(int64_t extent) const {
    std::vector<int64_t> sizes;
    for (int64_t s : {2, 4, 8, 16, 32, 64, 128, 256}) {
        if (s <= extent) sizes.push_back(s);
    }
    return sizes;
}

std::string ScheduleSpace::to_string() const {
    std::ostringstream oss;
    oss << "ScheduleSpace for " << op_->name << "\n";
    oss << "  Iter vars: " << get_iter_names().size() << "\n";
    oss << "  Rules: " << rules_.size() << "\n";
    oss << "  Estimated configs: " << estimated_size() << "\n";
    
    auto features = get_features();
    oss << "  Features: " << features.op_kind 
        << " dims=" << features.num_dims
        << " inputs=" << features.num_inputs
        << " outputs=" << features.num_outputs
        << " reduction=" << (features.is_reduction ? "yes" : "no") << "\n";
    
    return oss.str();
}

// ============================================================================
// ModuleScheduleSpace
// ============================================================================

ModuleScheduleSpace::ModuleScheduleSpace(TensorIRModule* mod) : module(mod) {
    for (const auto& op : mod->operations) {
        op_spaces[op.get()] = std::make_unique<ScheduleSpace>(op.get(), mod);
    }
}

ScheduleSpace* ModuleScheduleSpace::get_space(TensorOp* op) {
    auto it = op_spaces.find(op);
    if (it != op_spaces.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::unordered_map<TensorOp*, ScheduleConfig> ModuleScheduleSpace::random_sample(
    std::mt19937& rng) const {
    std::unordered_map<TensorOp*, ScheduleConfig> result;
    for (const auto& pair : op_spaces) {
        result[pair.first] = pair.second->random_sample(rng);
    }
    return result;
}

size_t ModuleScheduleSpace::estimated_total_size() const {
    size_t total = 1;
    for (const auto& pair : op_spaces) {
        total *= pair.second->estimated_size();
    }
    return total;
}

} // namespace scheduler
} // namespace claw
