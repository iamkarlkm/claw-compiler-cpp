// auto_scheduler/schedule_cache.cpp - 调度缓存实现

#include "schedule_cache.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>

namespace claw {
namespace scheduler {

// ============================================================================
// OpSignature
// ============================================================================

std::string OpSignature::from_op(TensorOp* op, const TensorIRModule* module) {
    std::ostringstream oss;
    oss << op->name << "#";
    
    switch (op->kind) {
        case TensorOp::OpKind::Compute: oss << "C"; break;
        case TensorOp::OpKind::Matmul: oss << "M"; break;
        case TensorOp::OpKind::Conv2d: oss << "V"; break;
        case TensorOp::OpKind::Pool2d: oss << "P"; break;
        case TensorOp::OpKind::Reduce: oss << "R"; break;
        case TensorOp::OpKind::Cast: oss << "T"; break;
        case TensorOp::OpKind::Broadcast: oss << "B"; break;
        case TensorOp::OpKind::Slice: oss << "S"; break;
    }
    
    // 输入形状
    for (const auto* in : op->inputs) {
        if (in) {
            oss << "_i" << shape_signature(in->shape);
        }
    }
    
    // 输出形状
    for (const auto* out : op->outputs) {
        if (out) {
            oss << "_o" << shape_signature(out->shape);
        }
    }
    
    // 迭代变量
    for (const auto* iv : op->iter_vars) {
        if (iv) {
            oss << "_" << iv->name;
            if (std::holds_alternative<int64_t>(iv->range.extent)) {
                oss << std::get<int64_t>(iv->range.extent);
            } else {
                oss << "S";
            }
        }
    }
    
    return oss.str();
}

std::string OpSignature::fuzzy(TensorOp* op, const TensorIRModule* module) {
    std::ostringstream oss;
    
    switch (op->kind) {
        case TensorOp::OpKind::Compute: oss << "compute"; break;
        case TensorOp::OpKind::Matmul: oss << "matmul"; break;
        case TensorOp::OpKind::Conv2d: oss << "conv2d"; break;
        case TensorOp::OpKind::Pool2d: oss << "pool2d"; break;
        case TensorOp::OpKind::Reduce: oss << "reduce"; break;
        default: oss << "other"; break;
    }
    
    // 模糊形状：只保留秩
    oss << "_r" << op->inputs.size() << "x" << op->outputs.size();
    for (const auto* in : op->inputs) {
        if (in) oss << "_" << fuzzy_shape_signature(in->shape);
    }
    for (const auto* out : op->outputs) {
        if (out) oss << "_" << fuzzy_shape_signature(out->shape);
    }
    
    return oss.str();
}

std::string OpSignature::shape_signature(const DimList& shape) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < shape.size(); i++) {
        if (i > 0) oss << ",";
        if (std::holds_alternative<int64_t>(shape[i])) {
            oss << std::get<int64_t>(shape[i]);
        } else {
            oss << "S";
        }
    }
    oss << "]";
    return oss.str();
}

std::string OpSignature::fuzzy_shape_signature(const DimList& shape) {
    std::ostringstream oss;
    oss << "r" << shape.size();  // 只保留秩
    return oss.str();
}

// ============================================================================
// ScheduleCache
// ============================================================================

ScheduleCache::ScheduleCache() : max_size_(10000) {}

ScheduleCache::ScheduleCache(size_t max_size) : max_size_(max_size) {}

bool ScheduleCache::find(const std::string& op_sig,
                         const std::string& config_sig,
                         CacheEntry& out) const {
    std::shared_lock lock(mutex_);
    
    auto it = cache_.find(op_sig);
    if (it == cache_.end()) {
        misses_++;
        return false;
    }
    
    auto jt = it->second.find(config_sig);
    if (jt == it->second.end()) {
        misses_++;
        return false;
    }
    
    out = jt->second;
    out.hit_count++;
    hits_++;
    return true;
}

void ScheduleCache::insert(const CacheEntry& entry) {
    std::unique_lock lock(mutex_);
    cache_[entry.op_signature][entry.config_signature] = entry;
    evict_if_needed();
}

void ScheduleCache::insert(const std::string& op_sig,
                           const std::string& config_sig,
                           double measured_time,
                           bool is_valid) {
    CacheEntry entry(config_sig, op_sig, measured_time);
    entry.is_valid = is_valid;
    insert(entry);
}

void ScheduleCache::insert_batch(const std::string& op_sig,
                                 const std::vector<std::pair<std::string, double>>& results) {
    std::unique_lock lock(mutex_);
    for (const auto& pair : results) {
        CacheEntry entry(pair.first, op_sig, pair.second);
        entry.is_valid = true;
        cache_[op_sig][pair.first] = entry;
    }
    evict_if_needed();
}

std::optional<CacheEntry> ScheduleCache::get_best(const std::string& op_sig) const {
    std::shared_lock lock(mutex_);
    
    auto it = cache_.find(op_sig);
    if (it == cache_.end() || it->second.empty()) {
        return std::nullopt;
    }
    
    const CacheEntry* best = nullptr;
    for (const auto& pair : it->second) {
        if (pair.second.is_valid && (!best || pair.second.measured_time_ms < best->measured_time_ms)) {
            best = &pair.second;
        }
    }
    
    if (best) return *best;
    return std::nullopt;
}

std::vector<CacheEntry> ScheduleCache::get_top_k(const std::string& op_sig, int k) const {
    std::shared_lock lock(mutex_);
    
    std::vector<CacheEntry> results;
    auto it = cache_.find(op_sig);
    if (it == cache_.end()) return results;
    
    for (const auto& pair : it->second) {
        if (pair.second.is_valid) {
            results.push_back(pair.second);
        }
    }
    
    std::sort(results.begin(), results.end(),
              [](const CacheEntry& a, const CacheEntry& b) {
                  return a.measured_time_ms < b.measured_time_ms;
              });
    
    if (static_cast<int>(results.size()) > k) {
        results.resize(k);
    }
    return results;
}

ScheduleCache::Stats ScheduleCache::get_stats() const {
    std::shared_lock lock(mutex_);
    
    Stats s;
    s.total_hits = hits_;
    s.total_misses = misses_;
    s.num_ops = cache_.size();
    
    for (const auto& pair : cache_) {
        s.total_entries += pair.second.size();
    }
    
    size_t total = s.total_hits + s.total_misses;
    s.hit_rate = total > 0 ? static_cast<double>(s.total_hits) / total : 0.0;
    
    return s;
}

void ScheduleCache::clear() {
    std::unique_lock lock(mutex_);
    cache_.clear();
    hits_ = 0;
    misses_ = 0;
}

bool ScheduleCache::save(const std::string& filepath) const {
    std::shared_lock lock(mutex_);
    
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    // 简单文本格式：每行一个条目
    for (const auto& op_pair : cache_) {
        for (const auto& cfg_pair : op_pair.second) {
            const auto& entry = cfg_pair.second;
            file << entry.op_signature << "\t"
                 << entry.config_signature << "\t"
                 << entry.measured_time_ms << "\t"
                 << (entry.is_valid ? 1 : 0) << "\t"
                 << entry.hit_count << "\n";
        }
    }
    
    return true;
}

bool ScheduleCache::load(const std::string& filepath) {
    std::unique_lock lock(mutex_);
    
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string op_sig, cfg_sig;
        double time;
        int valid, hits;
        
        if (std::getline(iss, op_sig, '\t') &&
            std::getline(iss, cfg_sig, '\t') &&
            (iss >> time >> valid >> hits)) {
            CacheEntry entry(cfg_sig, op_sig, time);
            entry.is_valid = (valid != 0);
            entry.hit_count = hits;
            cache_[op_sig][cfg_sig] = entry;
        }
    }
    
    return true;
}

std::vector<std::string> ScheduleCache::get_op_signatures() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    for (const auto& pair : cache_) {
        result.push_back(pair.first);
    }
    return result;
}

void ScheduleCache::warmup(TensorOp* op, ScheduleSpace* space) {
    if (!op || !space) return;
    
    // 插入默认配置
    auto default_cfg = space->get_default_config();
    auto op_sig = OpSignature::from_op(op, nullptr);
    
    CacheEntry dummy;
    if (!find(op_sig, default_cfg.signature(), dummy)) {
        // 如果没有缓存，插入一个占位条目（时间未知）
        insert(op_sig, default_cfg.signature(), -1.0, true);
    }
}

void ScheduleCache::evict_if_needed() {
    size_t total = 0;
    for (const auto& pair : cache_) {
        total += pair.second.size();
    }
    
    while (total > max_size_) {
        auto lru_key = find_lru_entry();
        if (lru_key.empty()) break;
        
        auto it = cache_.find(lru_key);
        if (it != cache_.end()) {
            // 找到该 op 下最久未命中的条目
            if (!it->second.empty()) {
                auto jt = it->second.begin();
                auto oldest = jt;
                for (++jt; jt != it->second.end(); ++jt) {
                    if (jt->second.timestamp < oldest->second.timestamp) {
                        oldest = jt;
                    }
                }
                it->second.erase(oldest);
                total--;
            }
            if (it->second.empty()) {
                cache_.erase(it);
            }
        }
    }
}

std::string ScheduleCache::find_lru_entry() const {
    std::string lru_op;
    auto oldest = std::chrono::system_clock::now();
    
    for (const auto& pair : cache_) {
        for (const auto& entry_pair : pair.second) {
            if (entry_pair.second.timestamp < oldest) {
                oldest = entry_pair.second.timestamp;
                lru_op = pair.first;
            }
        }
    }
    
    return lru_op;
}

// ============================================================================
// GlobalScheduleCache
// ============================================================================

ScheduleCache& GlobalScheduleCache::instance() {
    static ScheduleCache cache;
    return cache;
}

bool GlobalScheduleCache::save_to_file(const std::string& path) {
    return instance().save(path);
}

bool GlobalScheduleCache::load_from_file(const std::string& path) {
    return instance().load(path);
}

void GlobalScheduleCache::clear_all() {
    instance().clear();
}

} // namespace scheduler
} // namespace claw
