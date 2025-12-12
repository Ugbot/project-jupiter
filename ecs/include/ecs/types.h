#pragma once

/**
 * @file types.h
 * @brief Core ECS types: EntityId, ColumnId flags, alignment constants
 * 
 * Inspired by Apache Arrow's columnar design and Venus ECS double-buffered architecture.
 */

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <atomic>
#include <new>  // For std::bad_alloc

namespace jupiter::ecs {

// ============================================================================
// Entity Types
// ============================================================================

using EntityId = uint32_t;
constexpr EntityId INVALID_ENTITY = std::numeric_limits<EntityId>::max();

// ============================================================================
// Memory Alignment
// ============================================================================

// AVX-512 friendly alignment (also works for AVX2/SSE)
constexpr size_t SIMD_ALIGNMENT = 64;

// Cache line size for avoiding false sharing
constexpr size_t CACHE_LINE_SIZE = 64;

// Default batch size for kernel execution (fits L1 cache)
constexpr size_t DEFAULT_BATCH_SIZE = 256;

// ============================================================================
// Column Identifiers (bitmask for dirty tracking)
// ============================================================================

enum class ColumnId : uint32_t {
    None            = 0,
    
    // Transform columns
    Positions       = 1u << 0,
    Rotations       = 1u << 1,
    Scales          = 1u << 2,
    Transforms      = 1u << 3,  // Cached model matrices
    
    // Physics columns
    LinearVelocity  = 1u << 4,
    AngularVelocity = 1u << 5,
    MassInverse     = 1u << 6,
    InertiaInverse  = 1u << 7,
    ForceAccum      = 1u << 8,
    TorqueAccum     = 1u << 9,
    
    // Physics material
    LinearDamping   = 1u << 10,
    AngularDamping  = 1u << 11,
    Restitution     = 1u << 12,
    Friction        = 1u << 13,
    
    // Sleep system
    IsAwake         = 1u << 14,
    SleepCounter    = 1u << 15,
    MotionEnergy    = 1u << 16,
    
    // Collision
    SphereRadii     = 1u << 17,
    BoxHalfExtents  = 1u << 18,
    AABBs           = 1u << 19,
    
    // Rendering
    MaterialIds     = 1u << 20,
    MeshIds         = 1u << 21,
    Visibility      = 1u << 22,
    RenderObjects   = 1u << 23,
    
    // Entity metadata
    Ids             = 1u << 24,
    Flags           = 1u << 25,

    // Voxel columns
    ChunkPoolIndex    = 1u << 26,  // Index into VoxelWorld ChunkPool
    VoxelLodLevel     = 1u << 27,  // LOD state (0=full, 1=half, 2=distant, 255=unloaded)
    NetworkDirtyBits  = 1u << 28,  // Edit tracking for netcode delta sync

    // All columns mask
    All             = 0xFFFFFFFF
};

// Bitwise operators for ColumnId
inline constexpr ColumnId operator|(ColumnId a, ColumnId b) noexcept {
    return static_cast<ColumnId>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr ColumnId operator&(ColumnId a, ColumnId b) noexcept {
    return static_cast<ColumnId>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr ColumnId operator~(ColumnId a) noexcept {
    return static_cast<ColumnId>(~static_cast<uint32_t>(a));
}

inline constexpr bool hasColumn(ColumnId mask, ColumnId col) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(col)) != 0;
}

// ============================================================================
// Entity Flags (per-entity state bits)
// ============================================================================

enum class EntityFlags : uint32_t {
    None        = 0,
    Dirty       = 1u << 0,   // Transform needs recalculation
    Visible     = 1u << 1,   // Passed visibility test
    Physics     = 1u << 2,   // Has physics components
    Active      = 1u << 3,   // Entity is active
    Collidable  = 1u << 4,   // Participates in collision
    Replicated  = 1u << 5,   // Networked entity
    VoxelChunk  = 1u << 6,   // Entity is a voxel chunk (has voxel-specific columns)
    Static      = 1u << 16,  // Infinite mass physics body
};

inline constexpr EntityFlags operator|(EntityFlags a, EntityFlags b) noexcept {
    return static_cast<EntityFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr EntityFlags operator&(EntityFlags a, EntityFlags b) noexcept {
    return static_cast<EntityFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr bool hasFlag(EntityFlags mask, EntityFlags flag) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0;
}

// ============================================================================
// Kernel Execution Status (Arrow-inspired)
// ============================================================================

enum class Status : int32_t {
    Ok = 0,
    Error = -1,
    OutOfMemory = -2,
    InvalidArgument = -3,
    NotFound = -4,
    InvalidState = -5
};

inline bool isOk(Status s) noexcept { return s == Status::Ok; }

// ============================================================================
// Kernel Mode (thread safety classification)
// ============================================================================

enum class KernelMode : uint8_t {
    ReadOnly,   // Can run on any reader thread, parallel safe
    ReadWrite,  // Must run on writer thread, sequential
    Compute     // GPU dispatch, async
};

// ============================================================================
// Aligned Allocator for SIMD-friendly containers
// ============================================================================

template<typename T, size_t Alignment = SIMD_ALIGNMENT>
class AlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;
    
    template<typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    [[nodiscard]] T* allocate(size_type n) {
        if (n == 0) return nullptr;
        
        void* ptr = nullptr;
#if defined(_MSC_VER)
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
#else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0) {
            ptr = nullptr;
        }
#endif
        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, size_type) noexcept {
        if (ptr) {
#if defined(_MSC_VER)
            _aligned_free(ptr);
#else
            free(ptr);
#endif
        }
    }

    bool operator==(const AlignedAllocator&) const noexcept { return true; }
    bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

} // namespace jupiter::ecs

