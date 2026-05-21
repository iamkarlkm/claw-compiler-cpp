// ObjectPool - Fixed-size object pool for reducing heap allocations
// Used by ClawVM to reuse ArrayValue, TupleValue, and other hot objects

#ifndef CLAW_OBJECT_POOL_H
#define CLAW_OBJECT_POOL_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>

namespace claw {

/**
 * @brief Fixed-size object pool for fast allocation/deallocation.
 *
 * Objects are allocated from pre-allocated slabs. When an object is
 * released (via shared_ptr custom deleter), it is returned to the
 * free-list instead of being destroyed, allowing reuse.
 *
 * The pool must outlive all shared_ptrs created from it (enforced by
 * VMRuntime lifetime).
 */
template <typename T, size_t SlabSize = 256>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_slabs = 1) {
        for (size_t i = 0; i < initial_slabs; ++i) {
            add_slab();
        }
    }

    ~ObjectPool() {
        is_shutdown_ = true;
        // Destroy any objects still in free_list
        for (T* obj : free_list_) {
            obj->~T();
        }
        free_list_.clear();
        // Leak slab memory so that any late shared_ptr deleters
        // (triggered by objects in other pools) can safely call
        // ptr->~T() without use-after-free.
        for (auto& slab : slabs_) {
            slab.release();
        }
    }

    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /**
     * @brief Acquire a shared_ptr to a pooled object.
     *
     * If the pool has a free object, it is reused (after reset).
     * Otherwise a new object is allocated from the current slab or
     * a new slab is created.
     */
    template <typename... Args>
    std::shared_ptr<T> acquire(Args&&... args) {
        T* ptr = nullptr;
        if (!free_list_.empty()) {
            ptr = free_list_.back();
            free_list_.pop_back();
            // Reset the object in-place
            ptr->~T();
            new (ptr) T(std::forward<Args>(args)...);
        } else {
            ptr = allocate_from_slab();
            new (ptr) T(std::forward<Args>(args)...);
        }

        // Custom deleter returns object to pool instead of freeing
        return std::shared_ptr<T>(ptr, [this](T* p) { release(p); });
    }

    /** @brief Number of objects currently available in the free list. */
    size_t free_count() const { return free_list_.size(); }

    /** @brief Total number of objects allocated across all slabs. */
    size_t total_capacity() const { return slabs_.size() * SlabSize; }

    /** @brief Reset the pool, destroying all objects in the free list. */
    void clear() {
        for (T* obj : free_list_) {
            obj->~T();
        }
        free_list_.clear();
        slabs_.clear();
        next_slot_ = 0;
    }

    /** @brief Apply a function to every object in the free list.
     *
     * Useful for breaking cross-pool references before destruction.
     */
    template <typename Func>
    void for_each_free(Func&& func) {
        for (T* obj : free_list_) {
            func(*obj);
        }
    }

private:
    struct Slab {
        alignas(alignof(T)) char memory[SlabSize * sizeof(T)];
    };

    std::vector<std::unique_ptr<Slab>> slabs_;
    std::vector<T*> free_list_;
    size_t next_slot_ = 0;
    bool is_shutdown_ = false;

    void add_slab() {
        slabs_.push_back(std::make_unique<Slab>());
        next_slot_ = 0;
    }

    T* allocate_from_slab() {
        if (slabs_.empty() || next_slot_ >= SlabSize) {
            add_slab();
        }
        T* ptr = reinterpret_cast<T*>(slabs_.back()->memory + next_slot_ * sizeof(T));
        ++next_slot_;
        return ptr;
    }

    void release(T* ptr) {
        if (is_shutdown_) {
            // Pool is being destroyed; just leak the object.
            // Slab memory is leaked so there is no use-after-free.
            return;
        }
        // Return to free list for reuse
        // Note: we don't call destructor here; it will be called
        // when the object is reused (placement-new overwrites it)
        // or when the pool is destroyed.
        free_list_.push_back(ptr);
    }
};

/**
 * @brief Arena allocator for bulk temporary allocations.
 *
 * Allocates from a large pre-allocated block using bump-pointer.
 * All memory is freed together when reset() is called.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t initial_size = 64 * 1024)
        : initial_size_(initial_size) {
        reset();
    }

    ~ArenaAllocator() = default;

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /** @brief Allocate raw memory from the arena. */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        uintptr_t current = reinterpret_cast<uintptr_t>(current_);
        uintptr_t aligned = (current + alignment - 1) & ~(alignment - 1);
        size_t padding = aligned - current;

        if (current_offset_ + padding + size > capacity_) {
            // Fall back to malloc for oversized allocations
            if (size > capacity_ / 4) {
                void* fallback = std::malloc(size);
                fallback_allocs_.push_back(fallback);
                return fallback;
            }
            grow();
            // Recalculate after grow
            current = reinterpret_cast<uintptr_t>(current_);
            aligned = (current + alignment - 1) & ~(alignment - 1);
            padding = aligned - current;
        }

        current_ = reinterpret_cast<char*>(aligned);
        void* result = current_;
        current_ += size;
        current_offset_ += padding + size;
        return result;
    }

    /** @brief Construct an object in arena memory. */
    template <typename T, typename... Args>
    T* construct(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    /** @brief Reset the arena, freeing all allocated memory. */
    void reset() {
        buffers_.clear();
        fallback_allocs_.clear();
        add_buffer(initial_size_);
    }

    /** @brief Total bytes currently allocated in this arena. */
    size_t bytes_used() const { return current_offset_; }

private:
    size_t initial_size_;
    std::vector<std::unique_ptr<char[]>> buffers_;
    std::vector<void*> fallback_allocs_;
    char* current_ = nullptr;
    size_t current_offset_ = 0;
    size_t capacity_ = 0;

    void add_buffer(size_t size) {
        buffers_.push_back(std::make_unique<char[]>(size));
        current_ = buffers_.back().get();
        current_offset_ = 0;
        capacity_ = size;
    }

    void grow() {
        size_t new_size = capacity_ * 2;
        add_buffer(new_size);
    }
};

} // namespace claw

#endif // CLAW_OBJECT_POOL_H
