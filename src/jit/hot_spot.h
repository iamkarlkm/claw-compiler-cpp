/**
 * @file hot_spot.h
 * @brief Hot spot detection and profiling for JIT compilation
 * 
 * Identifies frequently executed code paths that benefit from JIT compilation.
 */

#ifndef CLAW_HOT_SPOT_H
#define CLAW_HOT_SPOT_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <algorithm>
#include "common/common.h"

namespace claw {

// Forward declarations
class Function;

/**
 * @brief Execution count and timing information for a code location
 */
struct ExecutionProfile {
    // Identification
    std::string location_id;      // Unique location identifier
    std::string source_location;  // Source code location (file:line)
    
    // Execution statistics
    uint64_t execution_count = 0;
    uint64_t total_cycles = 0;
    uint64_t total_time_us = 0;
    
    // Derived statistics
    double avg_cycles() const {
        return execution_count > 0 ? static_cast<double>(total_cycles) / execution_count : 0.0;
    }
    
    double avg_time_us() const {
        return execution_count > 0 ? static_cast<double>(total_time_us) / execution_count : 0.0;
    }
    
    double throughput() const {
        return total_time_us > 0 ? static_cast<double>(execution_count) / total_time_us : 0.0;
    }
    
    ExecutionProfile() = default;
    ExecutionProfile(const std::string& id, const std::string& src)
        : location_id(id), source_location(src) {}
};

/**
 * @brief Hotness level for JIT compilation decisions
 */
enum class HotnessLevel : uint8_t {
    COLD = 0,       // Not worth compiling
    WARM = 1,       // Might benefit from compilation
    HOT = 2,        // Should be compiled
    VERY_HOT = 3,   // High priority for optimization
    CRITICAL = 4    // Extremely hot - optimize aggressively
};

/**
 * @brief Configuration for hot spot detection
 */
struct HotSpotConfig {
    // Thresholds (adjusted for different modes)
    uint64_t min_executions_warm = 100;       // Minimum executions to be considered warm
    uint64_t min_executions_hot = 1000;       // Minimum executions to be considered hot
    uint64_t min_executions_very_hot = 5000;  // Minimum executions for very hot
    uint64_t min_executions_critical = 10000; // Minimum for critical
    
    // Time-based thresholds (microseconds)
    uint64_t min_total_time_warm = 1000;      // 1ms total execution
    uint64_t min_total_time_hot = 10000;      // 10ms total execution
    
    // Percentage thresholds
    double min_percentage_for_top = 1.0;      // Top X% of hot spots
    
    // Sampling
    bool enable_sampling = true;
    double sampling_rate = 0.01;              // Sample 1% of executions
    
    // Time window for decay (in executions)
    uint64_t decay_window = 10000;
    double decay_factor = 0.95;
    
    // Batch compilation threshold
    uint64_t batch_compilation_threshold = 50000;
    
    HotSpotConfig() = default;
    
    // Preset configurations
    static HotSpotConfig development() {
        HotSpotConfig config;
        config.min_executions_warm = 50;
        config.min_executions_hot = 200;
        config.min_executions_very_hot = 1000;
        config.min_executions_critical = 5000;
        return config;
    }
    
    static HotSpotConfig production() {
        HotSpotConfig config;
        config.min_executions_warm = 500;
        config.min_executions_hot = 5000;
        config.min_executions_very_hot = 20000;
        config.min_executions_critical = 100000;
        return config;
    }
    
    static HotSpotConfig benchmark() {
        HotSpotConfig config;
        config.min_executions_warm = 10;
        config.min_executions_hot = 50;
        config.min_executions_very_hot = 200;
        config.min_executions_critical = 1000;
        return config;
    }
};

/**
 * @brief Loop profile for loop-level hot spot detection
 */
struct LoopProfile {
    std::string loop_id;
    std::string source_location;
    
    uint64_t total_iterations = 0;
    uint64_t total_time_us = 0;
    
    // Loop characteristics
    uint32_t nest_level = 0;
    bool has_calls = false;
    bool has_branch = false;
    bool has_array_access = false;
    
    // Average iterations per invocation
    double avg_iterations() const {
        return total_iterations > 0 ? static_cast<double>(total_iterations) / 1 : 0.0;
    }
    
    LoopProfile() = default;
    LoopProfile(const std::string& id, const std::string& src)
        : loop_id(id), source_location(src) {}
};

/**
 * @brief Function profile for function-level hot spot detection
 */
struct FunctionProfile {
    std::string function_name;
    std::string source_location;
    
    uint64_t call_count = 0;
    uint64_t total_time_us = 0;
    
    // Inline decision factors
    bool is_small = false;        // < 10 instructions
    bool is_hot = false;          // Called frequently
    bool has_complex_control = false;
    bool has_recursive_call = false;
    
    double avg_time_us() const {
        return call_count > 0 ? static_cast<double>(total_time_us) / call_count : 0.0;
    }
    
    FunctionProfile() = default;
    FunctionProfile(const std::string& name, const std::string& src)
        : function_name(name), source_location(src) {}
};

/**
 * @brief Hot spot detector for JIT compilation
 * 
 * Tracks execution frequency and identifies hot code paths
 * that would benefit from JIT compilation.
 */
class HotSpotDetector {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    
    HotSpotDetector();
    explicit HotSpotDetector(const HotSpotConfig& config);
    ~HotSpotDetector();
    
    // === Recording Methods ===
    
    /**
     * @brief Record execution of a location
     * @param location_id Unique identifier for the code location
     * @param source_location Source code location (file:line)
     * @param cycles CPU cycles consumed
     * @param time_us Time in microseconds
     */
    void record_execution(const std::string& location_id,
                          const std::string& source_location,
                          uint64_t cycles = 0,
                          uint64_t time_us = 0);
    
    /**
     * @brief Record loop iteration
     * @param loop_id Loop identifier
     * @param source_location Source location
     * @param iterations Number of iterations
     * @param time_us Time taken
     */
    void record_loop(const std::string& loop_id,
                     const std::string& source_location,
                     uint64_t iterations,
                     uint64_t time_us = 0);
    
    /**
     * @brief Record function call
     * @param function_name Function name
     * @param source_location Source location
     * @param time_us Time taken
     */
    void record_function_call(const std::string& function_name,
                              const std::string& source_location,
                              uint64_t time_us = 0);
    
    /**
     * @brief Begin timing a region (for RAII-style timing)
     * @param location_id Location to time
     * @return Scope guard for timing
     */
    std::unique_ptr<class TimingGuard> begin_timing(const std::string& location_id);
    
    // === Query Methods ===
    
    /**
     * @brief Get execution profile for a location
     * @param location_id Location identifier
     * @return Pointer to profile, or nullptr if not found
     */
    std::shared_ptr<ExecutionProfile> get_profile(const std::string& location_id) const;
    
    /**
     * @brief Get hotness level for a location
     * @param location_id Location identifier
     * @return Hotness level
     */
    HotnessLevel get_hotness(const std::string& location_id) const;
    
    /**
     * @brief Get all hot locations
     * @param min_level Minimum hotness level to include
     * @return Vector of hot location IDs sorted by hotness
     */
    std::vector<std::string> get_hot_locations(HotnessLevel min_level = HotnessLevel::HOT) const;
    
    /**
     * @brief Get top N hottest locations
     * @param n Number of locations to return
     * @return Vector of hot location IDs
     */
    std::vector<std::string> get_top_hot(size_t n) const;
    
    /**
     * @brief Get all warm or hotter locations
     * @return Vector of warm+ location IDs
     */
    std::vector<std::string> get_warm_locations() const {
        return get_hot_locations(HotnessLevel::WARM);
    }
    
    /**
     * @brief Get loop profile
     * @param loop_id Loop identifier
     * @return Pointer to loop profile
     */
    std::shared_ptr<LoopProfile> get_loop_profile(const std::string& loop_id) const;
    
    /**
     * @brief Get function profile
     * @param function_name Function name
     * @return Pointer to function profile
     */
    std::shared_ptr<FunctionProfile> get_function_profile(const std::string& function_name) const;
    
    /**
     * @brief Check if a location should be JIT compiled
     * @param location_id Location to check
     * @return true if hot enough to compile
     */
    bool should_compile(const std::string& location_id) const;
    
    /**
     * @brief Check if compilation priority is high
     * @param location_id Location to check
     * @return true if high priority
     */
    bool is_high_priority(const std::string& location_id) const;
    
    // === Management ===
    
    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void set_config(const HotSpotConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    const HotSpotConfig& get_config() const { return config_; }
    
    /**
     * @brief Clear all profiling data
     */
    void clear();
    
    /**
     * @brief Enable/disable profiling
     * @param enabled True to enable
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
    /**
     * @brief Check if profiling is enabled
     */
    bool is_enabled() const { return enabled_; }
    
    /**
     * @brief Get statistics summary
     * @return Statistics string
     */
    std::string get_stats() const;
    
    /**
     * @brief Get detailed profile data
     * @return Map of location -> profile
     */
    std::unordered_map<std::string, std::shared_ptr<ExecutionProfile>> get_all_profiles() const;
    
    /**
     * @brief Serialize profiling data
     * @return Serialized data string
     */
    std::string serialize() const;
    
    /**
     * @brief Deserialize profiling data
     * @param data Serialized data
     * @return true if successful
     */
    bool deserialize(const std::string& data);
    
    /**
     * @brief Apply exponential decay to all profiles
     * @param factor Decay factor (0-1)
     */
    void apply_decay(double factor = 0.95);
    
    /**
     * @brief Reset counters (keep profiles but reset counts)
     */
    void reset_counters();
    
private:
    // Configuration
    HotSpotConfig config_;
    
    // Profiling data
    std::unordered_map<std::string, std::shared_ptr<ExecutionProfile>> profiles_;
    std::unordered_map<std::string, std::shared_ptr<LoopProfile>> loop_profiles_;
    std::unordered_map<std::string, std::shared_ptr<FunctionProfile>> function_profiles_;
    
    // State
    bool enabled_ = true;
    uint64_t total_executions_ = 0;
    TimePoint last_decay_time_;
    
    // Helper methods
    HotnessLevel compute_hotness(const ExecutionProfile& profile) const;
    void ensure_profile_exists(const std::string& location_id, const std::string& source_location);
    std::vector<std::string> sort_by_hotness(const std::vector<std::string>& locations) const;
};

/**
 * @brief RAII timing guard for automatic measurement
 */
class TimingGuard {
public:
    TimingGuard(HotSpotDetector* detector, const std::string& location_id, 
                const std::string& source_location);
    ~TimingGuard();
    
    // Non-copyable
    TimingGuard(const TimingGuard&) = delete;
    TimingGuard& operator=(const TimingGuard&) = delete;
    
    // Movable
    TimingGuard(TimingGuard&& other) noexcept;
    TimingGuard& operator=(TimingGuard&& other) noexcept;
    
    void release();  // Stop timing without recording
    
private:
    HotSpotDetector* detector_;
    std::string location_id_;
    std::string source_location_;
    HotSpotDetector::TimePoint start_time_;
    bool active_;
};

// ============================================================================
// Inline Implementations
// ============================================================================

inline HotnessLevel HotSpotDetector::compute_hotness(const ExecutionProfile& profile) const {
    if (profile.execution_count >= config_.min_executions_critical ||
        profile.total_time_us >= config_.min_total_time_hot * 10) {
        return HotnessLevel::CRITICAL;
    }
    if (profile.execution_count >= config_.min_executions_very_hot) {
        return HotnessLevel::VERY_HOT;
    }
    if (profile.execution_count >= config_.min_executions_hot) {
        return HotnessLevel::HOT;
    }
    if (profile.execution_count >= config_.min_executions_warm) {
        return HotnessLevel::WARM;
    }
    return HotnessLevel::COLD;
}

} // namespace claw

#endif // CLAW_HOT_SPOT_H
