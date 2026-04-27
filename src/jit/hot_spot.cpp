/**
 * @file hot_spot.cpp
 * @brief Hot spot detection implementation
 */

#include "jit/hot_spot.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace claw {

// ============================================================================
// HotSpotDetector Implementation
// ============================================================================

HotSpotDetector::HotSpotDetector() 
    : config_(HotSpotConfig::development()),
      last_decay_time_(Clock::now()) {}

HotSpotDetector::HotSpotDetector(const HotSpotConfig& config) 
    : config_(config),
      last_decay_time_(Clock::now()) {}

HotSpotDetector::~HotSpotDetector() = default;

// === Recording Methods ===

void HotSpotDetector::record_execution(const std::string& location_id,
                                        const std::string& source_location,
                                        uint64_t cycles,
                                        uint64_t time_us) {
    if (!enabled_) return;
    
    // Sampling check
    if (config_.enable_sampling) {
        // Simple random sampling - in production use proper RNG
        static uint64_t sample_counter = 0;
        sample_counter++;
        if (sample_counter % static_cast<uint64_t>(1.0 / config_.sampling_rate) != 0) {
            return;
        }
    }
    
    ensure_profile_exists(location_id, source_location);
    
    auto& profile = profiles_[location_id];
    profile->execution_count++;
    profile->total_cycles += cycles;
    profile->total_time_us += time_us;
    
    total_executions_++;
}

void HotSpotDetector::record_loop(const std::string& loop_id,
                                   const std::string& source_location,
                                   uint64_t iterations,
                                   uint64_t time_us) {
    if (!enabled_) return;
    
    auto it = loop_profiles_.find(loop_id);
    if (it == loop_profiles_.end()) {
        auto profile = std::make_shared<LoopProfile>(loop_id, source_location);
        profile->total_iterations = iterations;
        profile->total_time_us = time_us;
        loop_profiles_[loop_id] = profile;
    } else {
        auto& profile = it->second;
        profile->total_iterations += iterations;
        profile->total_time_us += time_us;
    }
    
    // Also record as execution for hot spot detection
    record_execution(loop_id, source_location, 0, time_us);
}

void HotSpotDetector::record_function_call(const std::string& function_name,
                                            const std::string& source_location,
                                            uint64_t time_us) {
    if (!enabled_) return;
    
    auto it = function_profiles_.find(function_name);
    if (it == function_profiles_.end()) {
        auto profile = std::make_shared<FunctionProfile>(function_name, source_location);
        profile->call_count = 1;
        profile->total_time_us = time_us;
        function_profiles_[function_name] = profile;
    } else {
        auto& profile = it->second;
        profile->call_count++;
        profile->total_time_us += time_us;
    }
}

std::unique_ptr<TimingGuard> HotSpotDetector::begin_timing(const std::string& location_id) {
    return std::make_unique<TimingGuard>(this, location_id, "");
}

// === Query Methods ===

std::shared_ptr<ExecutionProfile> HotSpotDetector::get_profile(const std::string& location_id) const {
    auto it = profiles_.find(location_id);
    if (it != profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

HotnessLevel HotSpotDetector::get_hotness(const std::string& location_id) const {
    auto profile = get_profile(location_id);
    if (!profile) return HotnessLevel::COLD;
    return compute_hotness(*profile);
}

std::vector<std::string> HotSpotDetector::get_hot_locations(HotnessLevel min_level) const {
    std::vector<std::string> result;
    
    for (const auto& [id, profile] : profiles_) {
        if (compute_hotness(*profile) >= min_level) {
            result.push_back(id);
        }
    }
    
    return sort_by_hotness(result);
}

std::vector<std::string> HotSpotDetector::get_top_hot(size_t n) const {
    auto hot = get_hot_locations(HotnessLevel::WARM);
    
    // Sort by execution count descending
    std::sort(hot.begin(), hot.end(), 
              [this](const std::string& a, const std::string& b) {
                  auto pa = profiles_.at(a);
                  auto pb = profiles_.at(b);
                  return pa->execution_count > pb->execution_count;
              });
    
    if (hot.size() > n) {
        hot.resize(n);
    }
    
    return hot;
}

std::shared_ptr<LoopProfile> HotSpotDetector::get_loop_profile(const std::string& loop_id) const {
    auto it = loop_profiles_.find(loop_id);
    if (it != loop_profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<FunctionProfile> HotSpotDetector::get_function_profile(const std::string& function_name) const {
    auto it = function_profiles_.find(function_name);
    if (it != function_profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

bool HotSpotDetector::should_compile(const std::string& location_id) const {
    auto hotness = get_hotness(location_id);
    return hotness >= HotnessLevel::HOT;
}

bool HotSpotDetector::is_high_priority(const std::string& location_id) const {
    auto hotness = get_hotness(location_id);
    return hotness >= HotnessLevel::VERY_HOT;
}

// === Management ===

void HotSpotDetector::set_config(const HotSpotConfig& config) {
    config_ = config;
}

void HotSpotDetector::clear() {
    profiles_.clear();
    loop_profiles_.clear();
    function_profiles_.clear();
    total_executions_ = 0;
}

std::string HotSpotDetector::get_stats() const {
    std::ostringstream oss;
    
    oss << "=== Hot Spot Detector Statistics ===\n";
    oss << "Enabled: " << (enabled_ ? "yes" : "no") << "\n";
    oss << "Total executions recorded: " << total_executions_ << "\n";
    oss << "Tracked locations: " << profiles_.size() << "\n";
    oss << "Tracked loops: " << loop_profiles_.size() << "\n";
    oss << "Tracked functions: " << function_profiles_.size() << "\n";
    
    // Hotness distribution
    size_t cold = 0, warm = 0, hot = 0, very_hot = 0, critical = 0;
    for (const auto& [id, profile] : profiles_) {
        switch (compute_hotness(*profile)) {
            case HotnessLevel::COLD: cold++; break;
            case HotnessLevel::WARM: warm++; break;
            case HotnessLevel::HOT: hot++; break;
            case HotnessLevel::VERY_HOT: very_hot++; break;
            case HotnessLevel::CRITICAL: critical++; break;
        }
    }
    
    oss << "\n--- Hotness Distribution ---\n";
    oss << "Critical: " << critical << "\n";
    oss << "Very Hot: " << very_hot << "\n";
    oss << "Hot: " << hot << "\n";
    oss << "Warm: " << warm << "\n";
    oss << "Cold: " << cold << "\n";
    
    // Top hot spots
    auto top = get_top_hot(10);
    if (!top.empty()) {
        oss << "\n--- Top 10 Hot Spots ---\n";
        for (size_t i = 0; i < top.size(); i++) {
            const auto& profile = profiles_.at(top[i]);
            oss << (i + 1) << ". " << top[i] << "\n";
            oss << "   Executions: " << profile->execution_count << "\n";
            oss << "   Total time: " << profile->total_time_us << "us\n";
            oss << "   Avg time: " << std::fixed << std::setprecision(2) 
                << profile->avg_time_us() << "us\n";
            
            auto hl = compute_hotness(*profile);
            oss << "   Hotness: ";
            switch (hl) {
                case HotnessLevel::CRITICAL: oss << "CRITICAL\n"; break;
                case HotnessLevel::VERY_HOT: oss << "VERY_HOT\n"; break;
                case HotnessLevel::HOT: oss << "HOT\n"; break;
                case HotnessLevel::WARM: oss << "WARM\n"; break;
                case HotnessLevel::COLD: oss << "COLD\n"; break;
            }
        }
    }
    
    return oss.str();
}

std::unordered_map<std::string, std::shared_ptr<ExecutionProfile>> 
HotSpotDetector::get_all_profiles() const {
    return profiles_;
}

std::string HotSpotDetector::serialize() const {
    std::ostringstream oss;
    oss << "{";
    
    // Locations
    oss << "\"locations\":[";
    bool first = true;
    for (const auto& [id, profile] : profiles_) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"id\":\"" << id << "\",\"execs\":" << profile->execution_count
            << ",\"time_us\":" << profile->total_time_us << "}";
    }
    oss << "],";
    
    // Loops
    oss << "\"loops\":[";
    first = true;
    for (const auto& [id, profile] : loop_profiles_) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"id\":\"" << id << "\",\"iters\":" << profile->total_iterations << "}";
    }
    oss << "],";
    
    // Functions
    oss << "\"functions\":[";
    first = true;
    for (const auto& [name, profile] : function_profiles_) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"name\":\"" << name << "\",\"calls\":" << profile->call_count << "}";
    }
    oss << "],";
    
    oss << "\"total_executions\":" << total_executions_ << "}";
    
    return oss.str();
}

bool HotSpotDetector::deserialize(const std::string& data) {
    clear();
    // Full deserialization would parse the JSON-like format
    return true;
}

void HotSpotDetector::apply_decay(double factor) {
    for (auto& [id, profile] : profiles_) {
        profile->execution_count = static_cast<uint64_t>(profile->execution_count * factor);
        profile->total_cycles = static_cast<uint64_t>(profile->total_cycles * factor);
        profile->total_time_us = static_cast<uint64_t>(profile->total_time_us * factor);
    }
    
    last_decay_time_ = Clock::now();
}

void HotSpotDetector::reset_counters() {
    for (auto& [id, profile] : profiles_) {
        profile->execution_count = 0;
        profile->total_cycles = 0;
        profile->total_time_us = 0;
    }
    total_executions_ = 0;
}

// === Private Helper Methods ===

void HotSpotDetector::ensure_profile_exists(const std::string& location_id, 
                                             const std::string& source_location) {
    if (profiles_.find(location_id) == profiles_.end()) {
        profiles_[location_id] = std::make_shared<ExecutionProfile>(location_id, source_location);
    }
}

std::vector<std::string> HotSpotDetector::sort_by_hotness(const std::vector<std::string>& locations) const {
    std::vector<std::string> sorted = locations;
    
    std::sort(sorted.begin(), sorted.end(), 
              [this](const std::string& a, const std::string& b) {
                  auto profile_a = profiles_.at(a);
                  auto profile_b = profiles_.at(b);
                  
                  // First sort by hotness level
                  auto hl_a = compute_hotness(*profile_a);
                  auto hl_b = compute_hotness(*profile_b);
                  
                  if (hl_a != hl_b) {
                      return hl_a > hl_b;
                  }
                  
                  // Then by execution count
                  return profile_a->execution_count > profile_b->execution_count;
              });
    
    return sorted;
}

// ============================================================================
// TimingGuard Implementation
// ============================================================================

TimingGuard::TimingGuard(HotSpotDetector* detector, const std::string& location_id,
                         const std::string& source_location)
    : detector_(detector),
      location_id_(location_id),
      source_location_(source_location),
      start_time_(HotSpotDetector::Clock::now()),
      active_(true) {}

TimingGuard::~TimingGuard() {
    if (active_ && detector_) {
        auto end = HotSpotDetector::Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_time_);
        detector_->record_execution(location_id_, source_location_, 0, duration.count());
    }
}

TimingGuard::TimingGuard(TimingGuard&& other) noexcept
    : detector_(other.detector_),
      location_id_(std::move(other.location_id_)),
      source_location_(std::move(other.source_location_)),
      start_time_(other.start_time_),
      active_(other.active_) {
    other.active_ = false;
}

TimingGuard& TimingGuard::operator=(TimingGuard&& other) noexcept {
    if (this != &other) {
        detector_ = other.detector_;
        location_id_ = std::move(other.location_id_);
        source_location_ = std::move(other.source_location_);
        start_time_ = other.start_time_;
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

void TimingGuard::release() {
    active_ = false;
}

} // namespace claw
