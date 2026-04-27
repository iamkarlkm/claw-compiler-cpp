/**
 * @file inline_cache.cpp
 * @brief Inline cache implementation for JIT type specialization
 */

#include "jit/inline_cache.h"
#include <algorithm>
#include <sstream>

namespace claw {

// ============================================================================
// InlineCache Implementation
// ============================================================================

InlineCache::InlineCache() = default;

InlineCache::~InlineCache() = default;

// === Call Site Management ===

CallSiteInfo* InlineCache::get_or_create_call_site(const std::string& site_id,
                                                     const std::string& name,
                                                     uint32_t arity) {
    auto it = call_sites_.find(site_id);
    if (it != call_sites_.end()) {
        return &it->second;
    }
    
    // Create new call site
    CallSiteInfo info(name, arity);
    info.source_location = site_id;
    
    auto [iter, _] = call_sites_.emplace(site_id, std::move(info));
    return &iter->second;
}

CallSiteInfo* InlineCache::get_call_site(const std::string& site_id) {
    auto it = call_sites_.find(site_id);
    if (it != call_sites_.end()) {
        return &it->second;
    }
    return nullptr;
}

void InlineCache::record_call_type(const std::string& site_id,
                                    uint64_t type_sig,
                                    std::shared_ptr<CompiledCode> code,
                                    void* target) {
    if (!enabled_) return;
    
    auto* site = get_or_create_call_site(site_id, "", 0);
    if (!site) return;
    
    // Check if entry already exists
    for (size_t i = 0; i < site->monomorphic_count; i++) {
        if (site->monomorphic_entries[i].type_signature == type_sig) {
            // Update existing entry
            site->monomorphic_entries[i].compiled_code = code;
            site->monomorphic_entries[i].call_target = target;
            site->monomorphic_entries[i].hit_count++;
            site->monomorphic_entries[i].last_access = current_time_++;
            return;
        }
    }
    
    // Add new entry if space available
    if (site->monomorphic_count < CallSiteInfo::kMaxMonomorphicEntries) {
        ICEntry entry(type_sig, target);
        entry.compiled_code = std::move(code);
        entry.hit_count = 1;
        entry.last_access = current_time_++;
        
        site->monomorphic_entries[site->monomorphic_count++] = std::move(entry);
    } else {
        // Transition to megamorphic
        update_monomorphic_to_megamorphic(*site);
    }
}

std::shared_ptr<CompiledCode> InlineCache::lookup_call(const std::string& site_id,
                                                        uint64_t type_sig) {
    if (!enabled_) return nullptr;
    
    auto* site = get_call_site(site_id);
    if (!site) return nullptr;
    
    // Check monomorphic entries
    for (size_t i = 0; i < site->monomorphic_count; i++) {
        if (site->monomorphic_entries[i].type_signature == type_sig) {
            site->monomorphic_entries[i].hit_count++;
            site->monomorphic_entries[i].last_access = current_time_++;
            return site->monomorphic_entries[i].compiled_code;
        }
    }
    
    // Check megamorphic
    if (site->is_megamorphic) {
        return site->megamorphic_code;
    }
    
    return nullptr;
}

void InlineCache::record_call_hit(const std::string& site_id, uint64_t type_sig) {
    total_calls_++;
    total_hits_++;
    
    auto* site = get_call_site(site_id);
    if (!site) return;
    
    site->total_calls++;
    site->ic_hits++;
    
    // Update hit count for matching entry
    for (size_t i = 0; i < site->monomorphic_count; i++) {
        if (site->monomorphic_entries[i].type_signature == type_sig) {
            site->monomorphic_entries[i].hit_count++;
            break;
        }
    }
}

void InlineCache::record_call_miss(const std::string& site_id) {
    total_calls_++;
    total_misses_++;
    
    auto* site = get_call_site(site_id);
    if (!site) return;
    
    site->total_calls++;
    site->ic_misses++;
}

// === Field Access Management ===

FieldICEntry* InlineCache::get_or_create_field_ic(const std::string& object_id,
                                                    const std::string& field_name) {
    auto key = make_field_key(object_id, field_name);
    
    auto it = field_ics_.find(key);
    if (it != field_ics_.end()) {
        return &it->second;
    }
    
    FieldICEntry entry(field_name);
    auto [iter, _] = field_ics_.emplace(key, std::move(entry));
    return &iter->second;
}

void InlineCache::record_field_type(const std::string& object_id,
                                     const std::string& field_name,
                                     uint64_t type_sig,
                                     std::shared_ptr<CompiledCode> code,
                                     bool is_getter) {
    if (!enabled_) return;
    
    auto* entry = get_or_create_field_ic(object_id, field_name);
    if (!entry) return;
    
    if (is_getter) {
        entry->getter_code = std::move(code);
        entry->getter_target = nullptr; // Would be set to actual code address
    } else {
        entry->setter_code = std::move(code);
        entry->setter_target = nullptr;
    }
    
    entry->type_signature = type_sig;
    entry->hit_count++;
}

std::shared_ptr<CompiledCode> InlineCache::lookup_field(const std::string& object_id,
                                                          const std::string& field_name,
                                                          uint64_t type_sig,
                                                          bool is_getter) {
    if (!enabled_) return nullptr;
    
    auto key = make_field_key(object_id, field_name);
    auto it = field_ics_.find(key);
    if (it == field_ics_.end()) return nullptr;
    
    auto& entry = it->second;
    if (is_getter) {
        return entry.getter_code;
    } else {
        return entry.setter_code;
    }
}

// === Statistics and Management ===

std::string InlineCache::get_stats() const {
    std::ostringstream oss;
    
    oss << "=== Inline Cache Statistics ===\n";
    oss << "Enabled: " << (enabled_ ? "yes" : "no") << "\n";
    oss << "Total call sites: " << call_sites_.size() << "\n";
    oss << "Total field ICs: " << field_ics_.size() << "\n";
    oss << "Total calls: " << total_calls_ << "\n";
    oss << "IC hits: " << total_hits_ << "\n";
    oss << "IC misses: " << total_misses_ << "\n";
    
    if (total_calls_ > 0) {
        double hit_rate = static_cast<double>(total_hits_) / total_calls_ * 100.0;
        oss << "Overall hit rate: " << hit_rate << "%\n";
    }
    
    // Per-call-site stats
    oss << "\n--- Call Site Details ---\n";
    for (const auto& [id, site] : call_sites_) {
        oss << "Site: " << id << "\n";
        oss << "  Function: " << site.name << "\n";
        oss << "  Arity: " << site.arity << "\n";
        oss << "  Total calls: " << site.total_calls << "\n";
        oss << "  IC hits: " << site.ic_hits << "\n";
        oss << "  IC misses: " << site.ic_misses << "\n";
        
        if (site.is_monomorphic()) {
            oss << "  State: MONOMORPHIC\n";
        } else if (site.is_polymorphic()) {
            oss << "  State: POLYMORPHIC (" << site.monomorphic_count << " types)\n";
        } else if (site.is_megamorphic_state()) {
            oss << "  State: MEGAMORPHIC\n";
        } else {
            oss << "  State: UNINITIALIZED\n";
        }
        
        if (site.total_calls > 0) {
            double site_hit_rate = static_cast<double>(site.ic_hits) / site.total_calls * 100.0;
            oss << "  Hit rate: " << site_hit_rate << "%\n";
        }
    }
    
    // Field IC stats
    if (!field_ics_.empty()) {
        oss << "\n--- Field IC Details ---\n";
        for (const auto& [key, entry] : field_ics_) {
            oss << "Field: " << entry.field_name << " (hits: " << entry.hit_count << ")\n";
        }
    }
    
    return oss.str();
}

void InlineCache::clear() {
    call_sites_.clear();
    field_ics_.clear();
    total_calls_ = 0;
    total_hits_ = 0;
    total_misses_ = 0;
    current_time_ = 0;
}

void InlineCache::clear_call_site(const std::string& site_id) {
    call_sites_.erase(site_id);
}

std::string InlineCache::serialize() const {
    std::ostringstream oss;
    oss << "{";
    
    // Call sites
    oss << "\"callsites\":[";
    bool first_site = true;
    for (const auto& [id, site] : call_sites_) {
        if (!first_site) oss << ",";
        first_site = false;
        oss << "{\"id\":\"" << id << "\",\"name\":\"" << site.name << "\",\"arity\":" << site.arity
            << ",\"calls\":" << site.total_calls << ",\"hits\":" << site.ic_hits << "}";
    }
    oss << "],";
    
    // Field ICs
    oss << "\"field_ics\":[";
    bool first_field = true;
    for (const auto& [key, entry] : field_ics_) {
        if (!first_field) oss << ",";
        first_field = false;
        oss << "{\"field\":\"" << entry.field_name << "\",\"hits\":" << entry.hit_count << "}";
    }
    oss << "],";
    
    oss << "\"total_calls\":" << total_calls_ << ",\"total_hits\":" << total_hits_ 
        << ",\"total_misses\":" << total_misses_ << "}";
    
    return oss.str();
}

bool InlineCache::deserialize(const std::string& data) {
    // Clear existing data
    clear();
    
    // In a full implementation, parse the serialized data
    // For now, just return true to indicate we handled it
    return true;
}

// === Private Helper Methods ===

std::string InlineCache::make_field_key(const std::string& object_id, 
                                          const std::string& field_name) const {
    return object_id + "::" + field_name;
}

void InlineCache::update_monomorphic_to_megamorphic(CallSiteInfo& site) {
    site.is_megamorphic = true;
    
    // Find most frequently used entry and use it as megamorphic fallback
    size_t best_idx = 0;
    uint64_t best_count = 0;
    for (size_t i = 0; i < site.monomorphic_count; i++) {
        if (site.monomorphic_entries[i].hit_count > best_count) {
            best_count = site.monomorphic_entries[i].hit_count;
            best_idx = i;
        }
    }
    
    if (best_count > 0) {
        site.megamorphic_code = site.monomorphic_entries[best_idx].compiled_code;
    }
    
    // Clear monomorphic entries
    site.monomorphic_count = 0;
}

void InlineCache::evict_lru_entry(CallSiteInfo& site) {
    if (site.monomorphic_count == 0) return;
    
    // Find LRU entry (lowest last_access)
    size_t lru_idx = 0;
    uint64_t oldest_time = UINT64_MAX;
    
    for (size_t i = 0; i < site.monomorphic_count; i++) {
        if (site.monomorphic_entries[i].last_access < oldest_time) {
            oldest_time = site.monomorphic_entries[i].last_access;
            lru_idx = i;
        }
    }
    
    // Shift entries to remove LRU
    for (size_t i = lru_idx; i < site.monomorphic_count - 1; i++) {
        site.monomorphic_entries[i] = site.monomorphic_entries[i + 1];
    }
    site.monomorphic_count--;
}

} // namespace claw
