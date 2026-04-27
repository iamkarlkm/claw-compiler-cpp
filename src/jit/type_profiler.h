/**
 * @file type_profiler.h
 * @brief Type feedback profiler for JIT compilation
 * 
 * Collects type information at runtime to guide JIT optimization decisions.
 */

#ifndef CLAW_TYPE_PROFILER_H
#define CLAW_TYPE_PROFILER_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include "common/common.h"

namespace claw {

// Forward declarations
class Value;

/**
 * @brief Represents a runtime type with frequency count
 */
struct TypeProfile {
    enum class TypeKind : uint8_t {
        NIL = 0,
        BOOL = 1,
        INT = 2,
        FLOAT = 3,
        STRING = 4,
        ARRAY = 5,
        TUPLE = 6,
        TENSOR = 7,
        FUNCTION = 8,
        CLOSURE = 9,
        POINTER = 10,
        UNKNOWN = 255
    };
    
    TypeKind kind = TypeKind::UNKNOWN;
    uint64_t count = 0;
    double confidence = 0.0;
    
    // For arrays/tensors: element type profile
    std::shared_ptr<TypeProfile> element_type;
    
    // For functions: parameter type profiles
    std::vector<std::shared_ptr<TypeProfile>> param_types;
    
    TypeProfile() = default;
    explicit TypeProfile(TypeKind k) : kind(k), count(1) {}
};

/**
 * @brief Profile for a single variable location
 */
struct VariableProfile {
    std::string name;
    std::unordered_map<TypeProfile::TypeKind, uint64_t> type_counts;
    uint64_t total_samples = 0;
    
    // Shape info for tensors (if applicable)
    std::vector<std::vector<int64_t>> observed_shapes;
    
    void record_type(TypeProfile::TypeKind kind);
    TypeProfile::TypeKind get_most_likely_type() const;
    double get_confidence(TypeProfile::TypeKind kind) const;
};

/**
 * @brief Profile for a call site
 */
struct CallSiteProfile {
    std::string function_name;
    uint64_t call_count = 0;
    
    // Parameter type profiles (indexed by parameter position)
    std::vector<std::shared_ptr<VariableProfile>> param_profiles;
    
    // Return type profile
    std::shared_ptr<TypeProfile> return_type_profile;
    
    // Monomorphic vs polymorphic
    bool is_monomorphic() const;
    bool is_polymorphic() const;
    TypeProfile::TypeKind get_most_likely_return_type() const;
};

/**
 * @brief Profile for a loop (hotspot detection)
 */
struct LoopProfile {
    uint64_t iteration_count = 0;
    uint64_t total_execution_time_us = 0;
    
    // Type stability within loop
    bool is_type_stable = true;
    std::unordered_map<std::string, TypeProfile::TypeKind> induction_variable_types;
    
    // Average iteration time
    double avg_iteration_time_us() const;
};

/**
 * @brief Type profiler for collecting runtime type information
 * 
 * Thread-safe profiler that collects type feedback during interpretation
 * and uses it to guide JIT compilation decisions.
 */
class TypeProfiler {
public:
    TypeProfiler();
    ~TypeProfiler();
    
    // === Global type recording ===
    
    /**
     * @brief Record a type at a specific program location
     * @param location_id Unique identifier for the location (e.g., "func:var")
     * @param value The runtime value
     */
    void record_type(const std::string& location_id, const Value* value);
    
    /**
     * @brief Record a type at a call site
     * @param call_id Unique call site identifier
     * @param function_name Name of the function being called
     * @param args Arguments passed to the function
     */
    void record_call(const std::string& call_id, 
                     const std::string& function_name,
                     const std::vector<const Value*>& args);
    
    /**
     * @brief Record return type for a function
     * @param function_name Name of the function
     * @param return_value The return value
     */
    void record_return(const std::string& function_name, const Value* return_value);
    
    /**
     * @brief Record loop iteration
     * @param loop_id Unique loop identifier
     * @param iteration_time_us Time taken for this iteration
     */
    void record_loop_iteration(const std::string& loop_id, 
                               uint64_t iteration_time_us);
    
    // === Query methods ===
    
    /**
     * @brief Get the type profile for a location
     * @param location_id Location identifier
     * @return Pointer to type profile, or nullptr if not found
     */
    std::shared_ptr<TypeProfile> get_type_profile(const std::string& location_id) const;
    
    /**
     * @brief Get the call site profile
     * @param call_id Call site identifier
     * @return Pointer to call site profile, or nullptr if not found
     */
    std::shared_ptr<CallSiteProfile> get_call_site_profile(const std::string& call_id) const;
    
    /**
     * @brief Get the loop profile
     * @param loop_id Loop identifier
     * @return Pointer to loop profile, or nullptr if not found
     */
    std::shared_ptr<LoopProfile> get_loop_profile(const std::string& loop_id) const;
    
    /**
     * @brief Check if a location should be JIT compiled
     * @param location_id Location to check
     * @return true if the location is hot enough to compile
     */
    bool should_jit_compile(const std::string& location_id) const;
    
    /**
     * @brief Check if a call site should be inlined
     * @param call_id Call site to check
     * @return true if the call site is monomorphic and hot
     */
    bool should_inline_call(const std::string& call_id) const;
    
    /**
     * @brief Get all hot locations
     * @return Vector of hot location IDs
     */
    std::vector<std::string> get_hot_locations() const;
    
    /**
     * @brief Get all polymorphic call sites
     * @return Vector of polymorphic call site IDs
     */
    std::vector<std::string> get_polymorphic_calls() const;
    
    // === Management ===
    
    /**
     * @brief Clear all profiling data
     */
    void clear();
    
    /**
     * @brief Enable/disable profiling
     * @param enabled True to enable, false to disable
     */
    void set_enabled(bool enabled);
    
    /**
     * @brief Get profiling enabled state
     * @return true if profiling is enabled
     */
    bool is_enabled() const;
    
    /**
     * @brief Get profiling statistics
     * @return String with profiling statistics
     */
    std::string get_stats() const;
    
    /**
     * @brief Serialize profiling data for persistence
     * @return Serialized profile data
     */
    std::string serialize() const;
    
    /**
     * @brief Load profiling data from serialized form
     * @param data Serialized profile data
     * @return true if successful
     */
    bool deserialize(const std::string& data);
    
private:
    // Internal data structures
    std::unordered_map<std::string, std::shared_ptr<VariableProfile>> variable_profiles_;
    std::unordered_map<std::string, std::shared_ptr<CallSiteProfile>> call_site_profiles_;
    std::unordered_map<std::string, std::shared_ptr<LoopProfile>> loop_profiles_;
    
    // Hotness thresholds
    static constexpr uint64_t kMinSamplesForProfile = 10;
    static constexpr uint64_t kMinCallsForJIT = 100;
    static constexpr uint64_t kMinIterationsForLoopJIT = 50;
    static constexpr double kMonomorphicThreshold = 0.95;
    
    // Profiling state
    bool enabled_ = true;
    uint64_t total_samples_ = 0;
    
    // Helper methods
    TypeProfile::TypeKind value_to_type_kind(const Value* value) const;
    void update_type_counts(VariableProfile& profile, TypeProfile::TypeKind kind);
};

// Inline implementations

inline void VariableProfile::record_type(TypeProfile::TypeKind kind) {
    type_counts[kind]++;
    total_samples_++;
}

inline TypeProfile::TypeKind VariableProfile::get_most_likely_type() const {
    if (type_counts.empty()) return TypeProfile::TypeKind::UNKNOWN;
    
    TypeProfile::TypeKind max_kind = TypeProfile::TypeKind::UNKNOWN;
    uint64_t max_count = 0;
    
    for (const auto& [kind, count] : type_counts) {
        if (count > max_count) {
            max_count = count;
            max_kind = kind;
        }
    }
    
    return max_kind;
}

inline double VariableProfile::get_confidence(TypeProfile::TypeKind kind) const {
    if (total_samples_ == 0) return 0.0;
    
    auto it = type_counts.find(kind);
    if (it == type_counts.end()) return 0.0;
    
    return static_cast<double>(it->second) / total_samples_;
}

inline bool CallSiteProfile::is_monomorphic() const {
    for (const auto& param : param_profiles) {
        if (!param) continue;
        if (param->type_counts.size() > 1) return false;
    }
    return param_profiles.size() > 0;
}

inline bool CallSiteProfile::is_polymorphic() const {
    return !is_monomorphic();
}

inline TypeProfile::TypeKind CallSiteProfile::get_most_likely_return_type() const {
    if (!return_type_profile) return TypeProfile::TypeKind::UNKNOWN;
    return return_type_profile->kind;
}

inline double LoopProfile::avg_iteration_time_us() const {
    if (iteration_count == 0) return 0.0;
    return static_cast<double>(total_execution_time_us) / iteration_count;
}

} // namespace claw

#endif // CLAW_TYPE_PROFILER_H
