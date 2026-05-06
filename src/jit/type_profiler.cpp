/**
 * @file type_profiler.cpp
 * @brief Type feedback profiler implementation
 */

#include "jit/type_profiler.h"
#include "vm/claw_vm.h"
#include "common/common.h"
#include <algorithm>
#include <cmath>

namespace claw {

// ============================================================================
// TypeProfiler Implementation
// ============================================================================

TypeProfiler::TypeProfiler() = default;

TypeProfiler::~TypeProfiler() = default;

void TypeProfiler::record_type(const std::string& location_id, const interpreter::Value* value) {
    if (!enabled_ || !value) return;
    
    auto kind = value_to_type_kind(value);
    
    // Get or create variable profile
    auto it = variable_profiles_.find(location_id);
    if (it == variable_profiles_.end()) {
        auto profile = std::make_shared<VariableProfile>();
        profile->name = location_id;
        profile->record_type(kind);
        variable_profiles_[location_id] = profile;
    } else {
        it->second->record_type(kind);
    }
    
    total_samples++;
}

void TypeProfiler::record_call(const std::string& call_id,
                                const std::string& function_name,
                                const std::vector<const interpreter::Value*>& args) {
    if (!enabled_) return;
    
    // Get or create call site profile
    auto it = call_site_profiles_.find(call_id);
    if (it == call_site_profiles_.end()) {
        auto profile = std::make_shared<CallSiteProfile>();
        profile->function_name = function_name;
        profile->call_count = 1;
        
        // Initialize parameter profiles
        for (size_t i = 0; i < args.size(); i++) {
            auto var_profile = std::make_shared<VariableProfile>();
            var_profile->name = call_id + ":param_" + std::to_string(i);
            if (args[i]) {
                var_profile->record_type(value_to_type_kind(args[i]));
            }
            profile->param_profiles.push_back(var_profile);
        }
        
        call_site_profiles_[call_id] = profile;
    } else {
        auto& profile = it->second;
        profile->call_count++;
        
        // Update parameter profiles
        if (profile->param_profiles.size() < args.size()) {
            profile->param_profiles.resize(args.size());
        }
        
        for (size_t i = 0; i < args.size(); i++) {
            if (!profile->param_profiles[i]) {
                profile->param_profiles[i] = std::make_shared<VariableProfile>();
                profile->param_profiles[i]->name = call_id + ":param_" + std::to_string(i);
            }
            
            if (args[i]) {
                profile->param_profiles[i]->record_type(value_to_type_kind(args[i]));
            }
        }
    }
}

void TypeProfiler::record_return(const std::string& function_name, const interpreter::Value* return_value) {
    if (!enabled_ || !return_value) return;
    
    // Record return type for all call sites calling this function
    for (auto& [call_id, profile] : call_site_profiles_) {
        if (profile->function_name == function_name) {
            if (!profile->return_type_profile) {
                profile->return_type_profile = std::make_shared<TypeProfile>();
            }
            profile->return_type_profile->kind = value_to_type_kind(return_value);
            profile->return_type_profile->count++;
        }
    }
}

void TypeProfiler::record_loop_iteration(const std::string& loop_id, uint64_t iteration_time_us) {
    if (!enabled_) return;
    
    auto it = loop_profiles_.find(loop_id);
    if (it == loop_profiles_.end()) {
        auto profile = std::make_shared<LoopProfile>();
        profile->iteration_count = 1;
        profile->total_execution_time_us = iteration_time_us;
        loop_profiles_[loop_id] = profile;
    } else {
        auto& profile = it->second;
        profile->iteration_count++;
        profile->total_execution_time_us += iteration_time_us;
    }
}

std::shared_ptr<TypeProfile> TypeProfiler::get_type_profile(const std::string& location_id) const {
    auto it = variable_profiles_.find(location_id);
    if (it != variable_profiles_.end()) {
        auto profile = std::make_shared<TypeProfile>();
        profile->kind = it->second->get_most_likely_type();
        profile->count = it->second->total_samples;
        profile->confidence = it->second->get_confidence(profile->kind);
        return profile;
    }
    return nullptr;
}

std::shared_ptr<CallSiteProfile> TypeProfiler::get_call_site_profile(const std::string& call_id) const {
    auto it = call_site_profiles_.find(call_id);
    if (it != call_site_profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<LoopProfile> TypeProfiler::get_loop_profile(const std::string& loop_id) const {
    auto it = loop_profiles_.find(loop_id);
    if (it != loop_profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

bool TypeProfiler::should_jit_compile(const std::string& location_id) const {
    auto it = variable_profiles_.find(location_id);
    if (it == variable_profiles_.end()) return false;
    
    const auto& profile = it->second;
    return profile->total_samples >= kMinCallsForJIT;
}

bool TypeProfiler::should_inline_call(const std::string& call_id) const {
    auto profile = get_call_site_profile(call_id);
    if (!profile) return false;
    
    // Inline if monomorphic and hot
    return profile->is_monomorphic() && profile->call_count >= kMinCallsForJIT;
}

std::vector<std::string> TypeProfiler::get_hot_locations() const {
    std::vector<std::pair<std::string, uint64_t>> hot;
    
    for (const auto& [id, profile] : variable_profiles_) {
        if (profile->total_samples >= kMinCallsForJIT) {
            hot.emplace_back(id, profile->total_samples);
        }
    }
    
    // Sort by sample count descending
    std::sort(hot.begin(), hot.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<std::string> result;
    for (const auto& [id, _] : hot) {
        result.push_back(id);
    }
    
    return result;
}

std::vector<std::string> TypeProfiler::get_polymorphic_calls() const {
    std::vector<std::string> result;
    
    for (const auto& [id, profile] : call_site_profiles_) {
        if (profile->is_polymorphic()) {
            result.push_back(id);
        }
    }
    
    return result;
}

void TypeProfiler::clear() {
    variable_profiles_.clear();
    call_site_profiles_.clear();
    loop_profiles_.clear();
    total_samples = 0;
}

void TypeProfiler::set_enabled(bool enabled) {
    enabled_ = enabled;
}

bool TypeProfiler::is_enabled() const {
    return enabled_;
}

std::string TypeProfiler::get_stats() const {
    std::string result;
    result += "=== Type Profiler Statistics ===\n";
    result += "Enabled: " + std::string(enabled_ ? "yes" : "no") + "\n";
    result += "Total samples: " + std::to_string(total_samples) + "\n";
    result += "Tracked variables: " + std::to_string(variable_profiles_.size()) + "\n";
    result += "Tracked call sites: " + std::to_string(call_site_profiles_.size()) + "\n";
    result += "Tracked loops: " + std::to_string(loop_profiles_.size()) + "\n";
    
    // Hot locations
    auto hot = get_hot_locations();
    if (!hot.empty()) {
        result += "\nHot locations (count >= " + std::to_string(kMinCallsForJIT) + "):\n";
        for (const auto& id : hot) {
            auto profile = get_type_profile(id);
            if (profile) {
                result += "  " + id + ": " + std::to_string(profile->count) 
                        + " samples, confidence: " + std::to_string(profile->confidence) + "\n";
            }
        }
    }
    
    // Polymorphic calls
    auto poly = get_polymorphic_calls();
    if (!poly.empty()) {
        result += "\nPolymorphic call sites:\n";
        for (const auto& id : poly) {
            auto profile = get_call_site_profile(id);
            if (profile) {
                result += "  " + id + ": " + std::to_string(profile->call_count) + " calls\n";
            }
        }
    }
    
    return result;
}

std::string TypeProfiler::serialize() const {
    // Simple JSON-like serialization
    std::string result = "{";
    
    // Variable profiles
    result += "\"variables\":[";
    bool first = true;
    for (const auto& [id, profile] : variable_profiles_) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":\"" + id + "\",\"samples\":" + std::to_string(profile->total_samples) + "}";
    }
    result += "],";
    
    // Call site profiles
    result += "\"callsites\":[";
    first = true;
    for (const auto& [id, profile] : call_site_profiles_) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":\"" + id + "\",\"func\":\"" + profile->function_name + "\",\"calls\":" 
                + std::to_string(profile->call_count) + "}";
    }
    result += "],";
    
    // Loop profiles
    result += "\"loops\":[";
    first = true;
    for (const auto& [id, profile] : loop_profiles_) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":\"" + id + "\",\"iterations\":" + std::to_string(profile->iteration_count) + "}";
    }
    result += "],";
    
    result += "\"total_samples\":" + std::to_string(total_samples) + "}";
    
    return result;
}

bool TypeProfiler::deserialize([[maybe_unused]] const std::string& data) {
    // Simple deserialization - just clear and start fresh for now
    // In a full implementation, parse the JSON-like format
    clear();
    return true;
}

TypeProfile::TypeKind TypeProfiler::value_to_type_kind(const interpreter::Value* value) const {
    if (!value) return TypeProfile::TypeKind::NIL;

    // interpreter::Value is std::variant<std::monostate, int64_t, double,
    //                                    std::string, bool, char>
    if (std::holds_alternative<std::monostate>(*value)) {
        return TypeProfile::TypeKind::NIL;
    }
    if (std::holds_alternative<int64_t>(*value)) {
        return TypeProfile::TypeKind::INT;
    }
    if (std::holds_alternative<double>(*value)) {
        return TypeProfile::TypeKind::FLOAT;
    }
    if (std::holds_alternative<std::string>(*value)) {
        return TypeProfile::TypeKind::STRING;
    }
    if (std::holds_alternative<bool>(*value)) {
        return TypeProfile::TypeKind::BOOL;
    }
    if (std::holds_alternative<char>(*value)) {
        return TypeProfile::TypeKind::STRING;
    }

    return TypeProfile::TypeKind::UNKNOWN;
}

void TypeProfiler::update_type_counts(VariableProfile& profile, TypeProfile::TypeKind kind) {
    profile.type_counts[kind]++;
    profile.total_samples++;
}

} // namespace claw
