#pragma once

#ifdef _WIN32
    #ifdef MEMORY_EXPORTS
        #define MEMORY_API __declspec(dllexport)
    #elif defined(MEMORY_IMPORTS)
        #define MEMORY_API __declspec(dllimport)
    #else
        #define MEMORY_API
    #endif
#else
    #define MEMORY_API
#endif

#include <cstddef>
#include <cstdint>
#include <memory>

namespace jupiter {
namespace memory {

/**
 * @brief Base allocator interface
 */
class MEMORY_API IAllocator {
public:
    virtual ~IAllocator() = default;

    /**
     * @brief Allocate memory
     * @param size Size in bytes to allocate
     * @param alignment Memory alignment (must be power of 2)
     * @return Pointer to allocated memory, or nullptr on failure
     */
    virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;

    /**
     * @brief Deallocate memory
     * @param ptr Pointer to memory to deallocate
     * @param size Size of the allocation (for debugging/tracking)
     */
    virtual void deallocate(void* ptr, size_t size = 0) = 0;

    /**
     * @brief Get the total allocated memory size
     * @return Total bytes allocated
     */
    virtual size_t getTotalAllocated() const = 0;

    /**
     * @brief Reset allocator (if supported)
     */
    virtual void reset() {}
};

/**
 * @brief Linear allocator - fast allocation, no individual deallocation
 * All allocations are freed at once when reset
 */
class MEMORY_API LinearAllocator : public IAllocator {
public:
    LinearAllocator(size_t capacity);
    ~LinearAllocator() override;

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
    void deallocate(void* ptr, size_t size = 0) override;
    size_t getTotalAllocated() const override;
    void reset() override;

private:
    void* m_buffer;
    size_t m_capacity;
    size_t m_offset;
    size_t m_peakUsage;
};

/**
 * @brief Stack allocator - LIFO allocation/deallocation
 */
class MEMORY_API StackAllocator : public IAllocator {
public:
    StackAllocator(size_t capacity);
    ~StackAllocator() override;

    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
    void deallocate(void* ptr, size_t size = 0) override;
    size_t getTotalAllocated() const override;

    /**
     * @brief Get current allocation marker
     * @return Current stack position marker
     */
    uintptr_t getMarker() const;

    /**
     * @brief Free all allocations up to the specified marker
     * @param marker Marker to free up to
     */
    void freeToMarker(uintptr_t marker);

    /**
     * @brief Clear all allocations
     */
    void clear();

private:
    struct AllocationHeader {
        uintptr_t prevMarker;
        size_t adjustment;
    };

    void* m_buffer;
    size_t m_capacity;
    uintptr_t m_currentMarker;
    uintptr_t m_prevMarker;
};

/**
 * @brief Default allocator using standard library malloc/free
 */
class MEMORY_API DefaultAllocator : public IAllocator {
public:
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
    void deallocate(void* ptr, size_t size = 0) override;
    size_t getTotalAllocated() const override { return 0; } // Not tracked
};

/**
 * @brief Memory tracking utilities
 */
class MEMORY_API MemoryTracker {
public:
    struct AllocationInfo {
        size_t size;
        const char* file;
        int line;
    };
    static void initialize();
    static void shutdown();
    static void trackAllocation(void* ptr, size_t size, const char* file, int line);
    static void trackDeallocation(void* ptr);
    static void reportLeaks();
    static size_t getTotalAllocated();

private:
    static bool s_initialized;
    static size_t s_totalAllocated;
};

// Memory tracking macros
#ifdef MEMORY_TRACKING_ENABLED
    #define TRACK_ALLOC(ptr, size) MemoryTracker::trackAllocation(ptr, size, __FILE__, __LINE__)
    #define TRACK_DEALLOC(ptr) MemoryTracker::trackDeallocation(ptr)
    #define MEMORY_NEW(type) new (MemoryTracker::trackAllocation(nullptr, sizeof(type), __FILE__, __LINE__)) type
    #define MEMORY_DELETE(ptr) if (ptr) { MemoryTracker::trackDeallocation(ptr); delete ptr; }
#else
    #define TRACK_ALLOC(ptr, size)
    #define TRACK_DEALLOC(ptr)
    #define MEMORY_NEW(type) new type
    #define MEMORY_DELETE(ptr) delete ptr
#endif

/**
 * @brief Initialize the memory subsystem
 * @return true if initialization was successful, false otherwise
 */
MEMORY_API bool initialize();

/**
 * @brief Shutdown the memory subsystem
 */
MEMORY_API void shutdown();

/**
 * @brief Get the default allocator
 * @return Pointer to the default allocator
 */
MEMORY_API IAllocator* getDefaultAllocator();

} // namespace memory
} // namespace jupiter
