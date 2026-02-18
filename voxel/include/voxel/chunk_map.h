#pragma once

#include "voxel_types.h"
#include <atomic>
#include <cstring>

/**
 * @file chunk_map.h
 * @brief Spatial hash map for chunk coordinate -> pool index lookup
 *
 * Following Project Jupiter principles:
 * - Lock-free design using atomic operations
 * - Pre-allocated fixed-size hash table
 * - No runtime allocations
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Lock-free spatial hash map for chunk lookup
 *
 * Maps ChunkCoord to pool index. Uses open addressing with linear probing.
 * Fixed bucket count, supports concurrent reads and writes.
 */
class ChunkMap {
public:
    /// Invalid pool index (sentinel)
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    /// Number of hash buckets (power of 2 for fast modulo)
    static constexpr size_t BUCKET_COUNT = 8192;

    /// Maximum probes before giving up
    static constexpr size_t MAX_PROBES = 16;

    ChunkMap() = default;
    ~ChunkMap() = default;

    // Non-copyable, non-movable
    ChunkMap(const ChunkMap&) = delete;
    ChunkMap& operator=(const ChunkMap&) = delete;
    ChunkMap(ChunkMap&&) = delete;
    ChunkMap& operator=(ChunkMap&&) = delete;

    /**
     * @brief Initialize the map
     *
     * Clears all entries. Must be called before use.
     */
    void initialize() {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            buckets_[i].occupied.store(false, std::memory_order_relaxed);
            buckets_[i].poolIndex = INVALID_INDEX;
        }
        entryCount_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Shutdown the map
     */
    void shutdown() {
        initialize();  // Just clear everything
    }

    /**
     * @brief Find pool index for a chunk coordinate
     *
     * Thread-safe for concurrent reads.
     *
     * @param coord Chunk coordinate to find
     * @return Pool index, or INVALID_INDEX if not found
     */
    uint32_t find(const ChunkCoord& coord) const {
        const size_t startBucket = coord.hash() & (BUCKET_COUNT - 1);

        for (size_t probe = 0; probe < MAX_PROBES; ++probe) {
            const size_t bucket = (startBucket + probe) & (BUCKET_COUNT - 1);
            const Entry& entry = buckets_[bucket];

            if (!entry.occupied.load(std::memory_order_acquire)) {
                // Empty slot means key not found
                return INVALID_INDEX;
            }

            if (entry.coord == coord) {
                return entry.poolIndex;
            }
        }

        // Max probes reached, not found
        return INVALID_INDEX;
    }

    /**
     * @brief Insert a chunk coordinate -> pool index mapping
     *
     * Thread-safe using CAS for insertion.
     *
     * @param coord Chunk coordinate
     * @param poolIndex Pool index to associate
     * @return true if inserted, false if slot not available or already exists
     */
    bool insert(const ChunkCoord& coord, uint32_t poolIndex) {
        const size_t startBucket = coord.hash() & (BUCKET_COUNT - 1);

        for (size_t probe = 0; probe < MAX_PROBES; ++probe) {
            const size_t bucket = (startBucket + probe) & (BUCKET_COUNT - 1);
            Entry& entry = buckets_[bucket];

            bool expected = false;
            if (entry.occupied.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                // Successfully claimed empty slot
                entry.coord = coord;
                entry.poolIndex = poolIndex;
                entryCount_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            // Slot occupied, check if same key
            if (entry.coord == coord) {
                // Key already exists (duplicate insert)
                return false;
            }

            // Different key, continue probing
        }

        // Max probes reached, table too full
        return false;
    }

    /**
     * @brief Remove a chunk coordinate mapping
     *
     * Thread-safe using CAS for removal.
     * Note: This uses tombstone-free removal which may cause issues
     * with concurrent lookups. Use with caution.
     *
     * @param coord Chunk coordinate to remove
     * @return true if removed, false if not found
     */
    bool remove(const ChunkCoord& coord) {
        const size_t startBucket = coord.hash() & (BUCKET_COUNT - 1);

        for (size_t probe = 0; probe < MAX_PROBES; ++probe) {
            const size_t bucket = (startBucket + probe) & (BUCKET_COUNT - 1);
            Entry& entry = buckets_[bucket];

            if (!entry.occupied.load(std::memory_order_acquire)) {
                // Empty slot means key not found
                return false;
            }

            if (entry.coord == coord) {
                // Found it, mark as unoccupied
                entry.poolIndex = INVALID_INDEX;
                entry.occupied.store(false, std::memory_order_release);
                entryCount_.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }

        // Max probes reached, not found
        return false;
    }

    /**
     * @brief Check if coordinate exists in map
     *
     * @param coord Chunk coordinate
     * @return true if exists
     */
    bool contains(const ChunkCoord& coord) const {
        return find(coord) != INVALID_INDEX;
    }

    /**
     * @brief Get current entry count
     *
     * @return Number of entries in map
     */
    size_t size() const {
        return entryCount_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Check if map is empty
     *
     * @return true if empty
     */
    bool empty() const {
        return size() == 0;
    }

    /**
     * @brief Get load factor
     *
     * @return Ratio of entries to buckets
     */
    float loadFactor() const {
        return static_cast<float>(size()) / BUCKET_COUNT;
    }

    /**
     * @brief Iterate over all occupied entries
     *
     * @param callback Function called for each entry (coord, poolIndex)
     *                 Return false to stop iteration early
     */
    template<typename Callback>
    void forEach(Callback&& callback) const {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            const Entry& entry = buckets_[i];
            if (entry.occupied.load(std::memory_order_acquire)) {
                if (!callback(entry.coord, entry.poolIndex)) {
                    return;  // Early exit
                }
            }
        }
    }

    /**
     * @brief Collect all loaded chunk coordinates
     *
     * @param outCoords Output array
     * @param maxCount Maximum entries to collect
     * @return Number of entries collected
     */
    size_t collectLoadedChunks(ChunkCoord* outCoords, size_t maxCount) const {
        size_t count = 0;
        for (size_t i = 0; i < BUCKET_COUNT && count < maxCount; ++i) {
            const Entry& entry = buckets_[i];
            if (entry.occupied.load(std::memory_order_acquire)) {
                outCoords[count++] = entry.coord;
            }
        }
        return count;
    }

private:
    /// Hash table entry
    struct Entry {
        ChunkCoord coord;
        uint32_t poolIndex = INVALID_INDEX;
        std::atomic<bool> occupied{false};
        uint8_t padding[3];  // Alignment
    };

    /// Hash buckets
    alignas(64) Entry buckets_[BUCKET_COUNT];

    /// Current entry count
    std::atomic<size_t> entryCount_{0};
};

} // namespace voxel
} // namespace jupiter
