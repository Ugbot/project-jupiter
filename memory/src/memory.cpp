#include "memory/memory.h"
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <iostream>

namespace jupiter {
namespace memory {

// Helper functions
static size_t alignSize(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

static uintptr_t alignPointer(void* ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return aligned;
}

// LinearAllocator implementation
LinearAllocator::LinearAllocator(size_t capacity)
    : m_buffer(nullptr), m_capacity(capacity), m_offset(0), m_peakUsage(0) {
    m_buffer = std::malloc(capacity);
    if (!m_buffer) {
        throw std::bad_alloc();
    }
}

LinearAllocator::~LinearAllocator() {
    if (m_buffer) {
        std::free(m_buffer);
    }
}

void* LinearAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_buffer) + m_offset;
    uintptr_t alignedAddr = alignPointer(reinterpret_cast<void*>(currentAddr), alignment);
    size_t adjustment = alignedAddr - currentAddr;
    size_t totalSize = size + adjustment;

    if (m_offset + totalSize > m_capacity) {
        return nullptr; // Out of memory
    }

    void* result = reinterpret_cast<void*>(alignedAddr);
    m_offset += totalSize;
    m_peakUsage = std::max(m_peakUsage, m_offset);

    TRACK_ALLOC(result, size);
    return result;
}

void LinearAllocator::deallocate(void* ptr, size_t size) {
    // Linear allocator doesn't support individual deallocation
    TRACK_DEALLOC(ptr);
}

size_t LinearAllocator::getTotalAllocated() const {
    return m_offset;
}

void LinearAllocator::reset() {
    m_offset = 0;
}

// StackAllocator implementation
StackAllocator::StackAllocator(size_t capacity)
    : m_buffer(nullptr), m_capacity(capacity), m_currentMarker(0), m_prevMarker(0) {
    m_buffer = std::malloc(capacity);
    if (!m_buffer) {
        throw std::bad_alloc();
    }
    m_currentMarker = reinterpret_cast<uintptr_t>(m_buffer);
    m_prevMarker = m_currentMarker;
}

StackAllocator::~StackAllocator() {
    if (m_buffer) {
        std::free(m_buffer);
    }
}

void* StackAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    // Store allocation header before the actual allocation
    uintptr_t headerAddr = m_currentMarker;
    AllocationHeader* header = reinterpret_cast<AllocationHeader*>(headerAddr);

    // Move past header
    uintptr_t dataAddr = headerAddr + sizeof(AllocationHeader);
    uintptr_t alignedAddr = alignPointer(reinterpret_cast<void*>(dataAddr), alignment);
    size_t adjustment = alignedAddr - dataAddr;

    // Calculate total allocation size
    size_t totalSize = sizeof(AllocationHeader) + adjustment + size;

    if (m_currentMarker + totalSize > reinterpret_cast<uintptr_t>(m_buffer) + m_capacity) {
        return nullptr; // Out of memory
    }

    // Fill header
    header->prevMarker = m_prevMarker;
    header->adjustment = adjustment;

    // Update markers
    m_prevMarker = m_currentMarker;
    m_currentMarker += totalSize;

    void* result = reinterpret_cast<void*>(alignedAddr);
    TRACK_ALLOC(result, size);
    return result;
}

void StackAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr) {
        return;
    }

    // Find the allocation header
    uintptr_t ptrAddr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t headerAddr = ptrAddr - sizeof(AllocationHeader);
    AllocationHeader* header = reinterpret_cast<AllocationHeader*>(headerAddr);

    // Validate that this is the top allocation
    uintptr_t expectedMarker = headerAddr + sizeof(AllocationHeader) + header->adjustment + size;
    if (expectedMarker == m_currentMarker) {
        // This is the top allocation, free it
        m_currentMarker = headerAddr;
        m_prevMarker = header->prevMarker;
    }

    TRACK_DEALLOC(ptr);
}

size_t StackAllocator::getTotalAllocated() const {
    return m_currentMarker - reinterpret_cast<uintptr_t>(m_buffer);
}

uintptr_t StackAllocator::getMarker() const {
    return m_currentMarker;
}

void StackAllocator::freeToMarker(uintptr_t marker) {
    if (marker <= reinterpret_cast<uintptr_t>(m_buffer) ||
        marker > reinterpret_cast<uintptr_t>(m_buffer) + m_capacity) {
        return;
    }

    // Free all allocations up to the marker
    while (m_currentMarker > marker) {
        // Find header of current top allocation
        uintptr_t currentAddr = m_currentMarker;
        if (currentAddr <= reinterpret_cast<uintptr_t>(m_buffer)) {
            break;
        }

        // Move back to find the header (this is a simplified approach)
        // In practice, you'd need to store more metadata
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(m_prevMarker);
        m_currentMarker = m_prevMarker;
        m_prevMarker = header->prevMarker;
    }
}

void StackAllocator::clear() {
    m_currentMarker = reinterpret_cast<uintptr_t>(m_buffer);
    m_prevMarker = m_currentMarker;
}

// DefaultAllocator implementation
void* DefaultAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    void* ptr = nullptr;
#ifdef _WIN32
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif

    TRACK_ALLOC(ptr, size);
    return ptr;
}

void DefaultAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr) {
        return;
    }

    TRACK_DEALLOC(ptr);
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

// MemoryTracker implementation
bool MemoryTracker::s_initialized = false;
size_t MemoryTracker::s_totalAllocated = 0;
static std::unordered_map<void*, MemoryTracker::AllocationInfo> s_allocations;

void MemoryTracker::initialize() {
    if (s_initialized) {
        return;
    }

    s_allocations.clear();
    s_totalAllocated = 0;
    s_initialized = true;
}

void MemoryTracker::shutdown() {
    if (!s_initialized) {
        return;
    }

    reportLeaks();
    s_allocations.clear();
    s_totalAllocated = 0;
    s_initialized = false;
}

void MemoryTracker::trackAllocation(void* ptr, size_t size, const char* file, int line) {
    if (!s_initialized || !ptr) {
        return;
    }

    s_allocations[ptr] = {size, file, line};
    s_totalAllocated += size;
}

void MemoryTracker::trackDeallocation(void* ptr) {
    if (!s_initialized || !ptr) {
        return;
    }

    auto it = s_allocations.find(ptr);
    if (it != s_allocations.end()) {
        s_totalAllocated -= it->second.size;
        s_allocations.erase(it);
    } else {
        std::cerr << "MemoryTracker: Attempting to deallocate unknown pointer: " << ptr << std::endl;
    }
}

void MemoryTracker::reportLeaks() {
    if (!s_initialized) {
        return;
    }

    if (!s_allocations.empty()) {
        std::cerr << "MemoryTracker: " << s_allocations.size() << " memory leaks detected!" << std::endl;
        std::cerr << "Total leaked memory: " << s_totalAllocated << " bytes" << std::endl;

        for (const auto& pair : s_allocations) {
            std::cerr << "Leak: " << pair.second.size << " bytes at "
                      << pair.first << " (" << pair.second.file << ":" << pair.second.line << ")" << std::endl;
        }
    } else {
        std::cout << "MemoryTracker: No memory leaks detected." << std::endl;
    }
}

size_t MemoryTracker::getTotalAllocated() {
    if (!s_initialized) {
        return 0;
    }

    return s_totalAllocated;
}

// Global memory system
static DefaultAllocator s_defaultAllocator;

bool initialize() {
    MemoryTracker::initialize();
    return true;
}

void shutdown() {
    MemoryTracker::shutdown();
}

IAllocator* getDefaultAllocator() {
    return &s_defaultAllocator;
}

} // namespace memory
} // namespace jupiter
