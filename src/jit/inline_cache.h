/**
 * @file inline_cache.h
 * @brief Inline cache for JIT type specialization and method dispatch
 * 
 * Inspired by V8's inline cache (IC) mechanism for fast polymorphic calls.
 */

#ifndef CLAW_INLINE_CACHE_H
#define CLAW_INLINE_CACHE_H

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <optional>
#include "common/common.h"

namespace claw {

// Forward declarations
class Function;
class CompiledCode;

/**
 * @brief Inline cache entry for a call site
 * 
 * Stores compiled code for specific type combinations to enable
 * fast method dispatch without runtime lookup.
 */
struct ICEntry {
    // Type signature (encoded as bit pattern)
    uint64_t type_signature = 0;
    
    // Compiled code for this type combination
    std::shared_ptr<CompiledCode> compiled_code = nullptr;
    
    // Call target address (for direct jumps)
    void* call_target = nullptr;
    
    // Hit count for adaptive optimization
    uint64_t hit_count = 0;
    
    // Last access time (for LRU eviction)
    uint64_t last_access = 0;
    
    ICEntry() = default;
    
    ICEntry(uint64_t sig, void* target) 
        : type_signature(sig), call_target(target) {}
};

/**
 * @brief Call site information for inline cache
 */
struct CallSiteInfo {
    std::string name;           // Function/method name
    uint32_t arity = 0;         // Number of parameters
    std::string source_location; // Source code location
    
    // IC entries (monomorphic, megamorphic)
    static constexpr size_t kMaxMonomorphicEntries = 4;
    std::array<ICEntry, kMaxMonomorphicEntries> monomorphic_entries;
    size_t monomorphic_count = 0;
    
    // Megamorphic fallback
    std::shared_ptr<CompiledCode> megamorphic_code = nullptr;
    bool is_megamorphic = false;
    
    // Statistics
    uint64_t total_calls = 0;
    uint64_t ic_hits = 0;
    uint64_t ic_misses = 0;
    
    CallSiteInfo() = default;
    CallSiteInfo(const std::string& n, uint32_t a) : name(n), arity(a) {}
    
    /**
     * @brief Check if this call site is monomorphic (single type)
     */
    bool is_monomorphic() const {
        return monomorphic_count == 1 && !is_megamorphic;
    }
    
    /**
     * @brief Check if this call site is polymorphic (few types)
     */
    bool is_polymorphic() const {
        return monomorphic_count > 1 && monomorphic_count <= kMaxMonomorphicEntries && !is_megamorphic;
    }
    
    /**
     * @brief Check if this call site is megamorphic (many types)
     */
    bool is_megamorphic_state() const {
        return is_megamorphic;
    }
    
    /**
     * @brief Get hit rate
     */
    double hit_rate() const {
        if (total_calls == 0) return 0.0;
        return static_cast<double>(ic_hits) / total_calls;
    }
};

/**
 * @brief Field access inline cache (for property reads/writes)
 */
struct FieldICEntry {
    std::string field_name;
    uint64_t type_signature = 0;
    std::shared_ptr<CompiledCode> getter_code;
    std::shared_ptr<CompiledCode> setter_code;
    void* getter_target = nullptr;
    void* setter_target = nullptr;
    uint64_t hit_count = 0;
    
    FieldICEntry() = default;
    FieldICEntry(const std::string& name) : field_name(name) {}
};

/**
 * @brief Inline cache manager for JIT compilation
 * 
 * Manages inline caches for call sites and field accesses,
 * enabling fast polymorphic dispatch without runtime type checks.
 */
class InlineCache {
public:
    // Constants
    static constexpr size_t kMaxCallSites = 1024;
    static constexpr size_t kMaxFieldICs = 512;
    static constexpr uint64_t kMegamorphicThreshold = 4;
    static constexpr uint64_t kPolymorphicThreshold = 1;
    
    InlineCache();
    ~InlineCache();
    
    // === Call Site Management ===
    
    /**
     * @brief Get or create a call site IC
     * @param site_id Unique call site identifier
     * @param name Function/method name
     * @param arity Number of parameters
     * @return Pointer to call site info
     */
    CallSiteInfo* get_or_create_call_site(const std::string& site_id,
                                           const std::string& name,
                                           uint32_t arity);
    
    /**
     * @brief Get call site info
     * @param site_id Call site identifier
     * @return Pointer to call site info, or nullptr if not found
     */
    CallSiteInfo* get_call_site(const std::string& site_id);
    
    /**
     * @brief Record a type combination at a call site
     * @param site_id Call site identifier
     * @param type_sig Type signature (encoded)
     * @param code Compiled code for this type combination
     * @param target Code entry point
     */
    void record_call_type(const std::string& site_id,
                          uint64_t type_sig,
                          std::shared_ptr<CompiledCode> code,
                          void* target);
    
    /**
     * @brief Lookup compiled code for a type combination
     * @param site_id Call site identifier
     * @param type_sig Type signature
     * @return Compiled code if found, nullptr otherwise
     */
    std::shared_ptr<CompiledCode> lookup_call(const std::string& site_id,
                                               uint64_t type_sig);
    
    /**
     * @brief Record a cache hit
     * @param site_id Call site identifier
     * @param type_sig Type signature that matched
     */
    void record_call_hit(const std::string& site_id, uint64_t type_sig);
    
    /**
     * @brief Record a cache miss
     * @param site_id Call site identifier
     */
    void record_call_miss(const std::string& site_id);
    
    // === Field Access Management ===
    
    /**
     * @brief Get or create a field IC
     * @param object_id Object identifier
     * @param field_name Field name
     * @return Pointer to field IC entry
     */
    FieldICEntry* get_or_create_field_ic(const std::string& object_id,
                                           const std::string& field_name);
    
    /**
     * @brief Record field access type
     * @param object_id Object identifier
     * @param field_name Field name
     * @param type_sig Type signature
     * @param code Compiled code
     * @param is_getter true for getter, false for setter
     */
    void record_field_type(const std::string& object_id,
                           const std::string& field_name,
                           uint64_t type_sig,
                           std::shared_ptr<CompiledCode> code,
                           bool is_getter);
    
    /**
     * @brief Lookup field access code
     * @param object_id Object identifier
     * @param field_name Field name
     * @param type_sig Type signature
     * @param is_getter true for getter, false for setter
     * @return Compiled code if found
     */
    std::shared_ptr<CompiledCode> lookup_field(const std::string& object_id,
                                                 const std::string& field_name,
                                                 uint64_t type_sig,
                                                 bool is_getter);
    
    // === Statistics and Management ===
    
    /**
     * @brief Get call site statistics
     * @return Summary statistics string
     */
    std::string get_stats() const;
    
    /**
     * @brief Get total number of call sites
     */
    size_t num_call_sites() const { return call_sites_.size(); }
    
    /**
     * @brief Get total number of field ICs
     */
    size_t num_field_ics() const { return field_ics_.size(); }
    
    /**
     * @brief Clear all inline caches
     */
    void clear();
    
    /**
     * @brief Clear a specific call site
     * @param site_id Call site to clear
     */
    void clear_call_site(const std::string& site_id);
    
    /**
     * @brief Enable/disable inline caching
     * @param enabled True to enable
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
    /**
     * @brief Check if inline caching is enabled
     */
    bool is_enabled() const { return enabled_; }
    
    /**
     * @brief Serialize IC state for persistence
     */
    std::string serialize() const;
    
    /**
     * @brief Deserialize IC state
     * @param data Serialized data
     * @return true if successful
     */
    bool deserialize(const std::string& data);
    
private:
    // Call site ICs: site_id -> CallSiteInfo
    std::unordered_map<std::string, CallSiteInfo> call_sites_;
    
    // Field ICs: object_id + field_name -> FieldICEntry
    std::unordered_map<std::string, FieldICEntry> field_ics_;
    
    // Global statistics
    uint64_t total_calls_ = 0;
    uint64_t total_hits_ = 0;
    uint64_t total_misses_ = 0;
    
    // Configuration
    bool enabled_ = true;
    uint64_t current_time_ = 0;
    
    // Helper methods
    std::string make_field_key(const std::string& object_id, const std::string& field_name) const;
    void update_monomorphic_to_megamorphic(CallSiteInfo& site);
    void evict_lru_entry(CallSiteInfo& site);
};

// ============================================================================
// Inline Cache Helper Functions
// ============================================================================

/**
 * @brief Encode types into a compact signature
 * @param types Vector of type IDs
 * @return 64-bit type signature
 */
inline uint64_t encode_type_signature(const std::vector<uint32_t>& types) {
    uint64_t sig = 0;
    for (size_t i = 0; i < types.size() && i < 8; i++) {
        sig |= (static_cast<uint64_t>(types[i]) & 0xFF) << (i * 8);
    }
    return sig;
}

/**
 * @brief Decode type signature back to types
 * @param sig 64-bit type signature
 * @param num_types Number of types to decode
 * @return Vector of type IDs
 */
inline std::vector<uint32_t> decode_type_signature(uint64_t sig, size_t num_types) {
    std::vector<uint32_t> types;
    for (size_t i = 0; i < num_types; i++) {
        types.push_back((sig >> (i * 8)) & 0xFF);
    }
    return types;
}

/**
 * @brief Compute field access type signature
 * @param object_type Type of the object
 * @param field_offset Field offset in object
 * @return Combined type signature
 */
inline uint64_t compute_field_signature(uint32_t object_type, uint32_t field_offset) {
    return (static_cast<uint64_t>(object_type) << 32) | field_offset;
}

} // namespace claw

#endif // CLAW_INLINE_CACHE_H
