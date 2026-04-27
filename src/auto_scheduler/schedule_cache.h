// auto_scheduler/schedule_cache.h - 调度缓存系统
// 缓存已评估的调度配置，避免重复测量

#ifndef CLAW_SCHEDULE_CACHE_H
#define CLAW_SCHEDULE_CACHE_H

#include "schedule_space.h"
#include <unordered_map>
#include <shared_mutex>
#include <fstream>
#include <json/json.h>  // 可选：JSON序列化

namespace claw {
namespace scheduler {

// ============================================================================
// 缓存条目
// ============================================================================

struct CacheEntry {
    std::string config_signature;
    std::string op_signature;       // 操作特征签名
    double measured_time_ms = -1.0;
    double predicted_time_ms = -1.0;
    double throughput_gflops = 0.0;
    bool is_valid = false;
    int hit_count = 0;              // 缓存命中次数
    std::chrono::system_clock::time_point timestamp;
    
    CacheEntry() = default;
    CacheEntry(const std::string& cs, const std::string& os, double t)
        : config_signature(cs), op_signature(os), measured_time_ms(t),
          timestamp(std::chrono::system_clock::now()) {}
};

// ============================================================================
// 操作签名生成器
// ============================================================================

class OpSignature {
public:
    // 生成操作唯一签名（基于形状、类型、轴）
    static std::string from_op(TensorOp* op, const TensorIRModule* module);
    
    // 生成模糊签名（忽略具体 batch size，只保留秩）
    static std::string fuzzy(TensorOp* op, const TensorIRModule* module);
    
private:
    static std::string shape_signature(const DimList& shape);
    static std::string fuzzy_shape_signature(const DimList& shape);
};

// ============================================================================
// 调度缓存
// ============================================================================

class ScheduleCache {
public:
    ScheduleCache();
    explicit ScheduleCache(size_t max_size);
    
    // 查询缓存
    bool find(const std::string& op_sig,
              const std::string& config_sig,
              CacheEntry& out) const;
    
    // 插入/更新
    void insert(const CacheEntry& entry);
    void insert(const std::string& op_sig,
                const std::string& config_sig,
                double measured_time,
                bool is_valid = true);
    
    // 批量插入
    void insert_batch(const std::string& op_sig,
                      const std::vector<std::pair<std::string, double>>& results);
    
    // 获取最优配置
    std::optional<CacheEntry> get_best(const std::string& op_sig) const;
    
    // 获取 Top-K
    std::vector<CacheEntry> get_top_k(const std::string& op_sig, int k) const;
    
    // 缓存统计
    struct Stats {
        size_t total_entries = 0;
        size_t total_hits = 0;
        size_t total_misses = 0;
        double hit_rate = 0.0;
        size_t num_ops = 0;
    };
    Stats get_stats() const;
    
    // 清空
    void clear();
    
    // 持久化（简单文本格式）
    bool save(const std::string& filepath) const;
    bool load(const std::string& filepath);
    
    // 获取所有操作签名
    std::vector<std::string> get_op_signatures() const;
    
    // 预热：用默认配置填充缓存
    void warmup(TensorOp* op, ScheduleSpace* space);

private:
    mutable std::shared_mutex mutex_;
    
    // op_signature -> (config_signature -> entry)
    std::unordered_map<std::string, 
        std::unordered_map<std::string, CacheEntry>> cache_;
    
    size_t max_size_ = 10000;
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
    
    // LRU 淘汰
    void evict_if_needed();
    std::string find_lru_entry() const;
};

// ============================================================================
// 全局缓存管理器（单例）
// ============================================================================

class GlobalScheduleCache {
public:
    static ScheduleCache& instance();
    
    static bool save_to_file(const std::string& path);
    static bool load_from_file(const std::string& path);
    static void clear_all();

private:
    GlobalScheduleCache() = default;
};

} // namespace scheduler
} // namespace claw

#endif // CLAW_SCHEDULE_CACHE_H
