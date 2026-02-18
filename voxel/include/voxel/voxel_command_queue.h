#pragma once

/**
 * @file voxel_command_queue.h
 * @brief Aeron-style SPSC command queue for voxel operations
 *
 * Provides lock-free command queuing with sequence number tracking
 * for ordered processing and acknowledgement.
 */

#include "voxel_command.h"
#include <memory/queues.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <thread>

namespace jupiter {
namespace voxel {

/**
 * @brief Aeron-style SPSC command queue for voxel edits
 *
 * Features:
 * - Lock-free SPSC queue for producer/consumer pattern
 * - Sequence number tracking for ordered acknowledgement
 * - Batch drain for efficient processing
 * - Priority sorting on drain
 *
 * Usage:
 * - Game thread: enqueue() commands
 * - Voxel worker: drainAll() and process
 */
class VoxelCommandQueue {
public:
    /// Queue capacity (must be power of 2)
    static constexpr size_t CAPACITY = 4096;
    
    VoxelCommandQueue() = default;
    ~VoxelCommandQueue() = default;
    
    // Non-copyable, non-movable (contains queue)
    VoxelCommandQueue(const VoxelCommandQueue&) = delete;
    VoxelCommandQueue& operator=(const VoxelCommandQueue&) = delete;
    VoxelCommandQueue(VoxelCommandQueue&&) = delete;
    VoxelCommandQueue& operator=(VoxelCommandQueue&&) = delete;
    
    // ========================================================================
    // Producer Interface (Game Thread)
    // ========================================================================
    
    /**
     * @brief Enqueue a single command
     *
     * Assigns sequence number and pushes to queue.
     * Thread-safe for single producer.
     *
     * @param cmd Command to enqueue
     * @return true if successful, false if queue is full
     */
    bool enqueue(VoxelCommand cmd) {
        // Assign monotonic sequence number
        cmd.sequenceNumber = nextSequence_.fetch_add(1, std::memory_order_relaxed);
        
        if (!queue_.push(std::move(cmd))) {
            // Queue full - increment dropped counter
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        publishedSeq_.store(cmd.sequenceNumber, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Enqueue multiple commands as a batch
     *
     * @param cmds Array of commands to enqueue
     * @param count Number of commands
     * @return Number of commands successfully enqueued
     */
    size_t enqueueBatch(const VoxelCommand* cmds, size_t count) {
        size_t enqueued = 0;
        
        for (size_t i = 0; i < count; ++i) {
            if (enqueue(cmds[i])) {
                ++enqueued;
            } else {
                break;  // Queue full
            }
        }
        
        return enqueued;
    }
    
    // ========================================================================
    // Consumer Interface (Voxel Worker Thread)
    // ========================================================================
    
    /**
     * @brief Drain up to maxCount commands into output array
     *
     * @param out Output array (must have space for maxCount)
     * @param maxCount Maximum commands to drain
     * @return Number of commands drained
     */
    size_t drainTo(VoxelCommand* out, size_t maxCount) {
        size_t count = 0;
        
        while (count < maxCount) {
            if (!queue_.pop(out[count])) {
                break;  // Queue empty
            }
            
            consumedSeq_.store(out[count].sequenceNumber, std::memory_order_release);
            ++count;
        }
        
        return count;
    }
    
    /**
     * @brief Drain all available commands into vector
     *
     * Drains queue and optionally sorts by priority (descending).
     *
     * @param out Output vector (will be cleared first)
     * @param sortByPriority If true, sort by priority (highest first)
     * @return Number of commands drained
     */
    size_t drainAll(std::vector<VoxelCommand>& out, bool sortByPriority = true) {
        out.clear();
        out.reserve(256);  // Reasonable initial capacity
        
        VoxelCommand cmd;
        while (queue_.pop(cmd)) {
            consumedSeq_.store(cmd.sequenceNumber, std::memory_order_release);
            out.push_back(cmd);
        }
        
        if (sortByPriority && out.size() > 1) {
            // Stable sort to preserve sequence order within same priority
            std::stable_sort(out.begin(), out.end(),
                [](const VoxelCommand& a, const VoxelCommand& b) {
                    return a.priority > b.priority;  // Higher priority first
                });
        }
        
        return out.size();
    }
    
    /**
     * @brief Try to pop a single command
     *
     * @param out Output command
     * @return true if command was popped, false if queue empty
     */
    bool tryPop(VoxelCommand& out) {
        if (!queue_.pop(out)) {
            return false;
        }
        
        consumedSeq_.store(out.sequenceNumber, std::memory_order_release);
        return true;
    }
    
    // ========================================================================
    // Sequence Tracking (Aeron-style)
    // ========================================================================
    
    /**
     * @brief Get the last published sequence number
     *
     * Used by consumer to check if new commands are available.
     */
    uint64_t lastPublishedSequence() const {
        return publishedSeq_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get the last consumed sequence number
     *
     * Used by producer to confirm processing.
     */
    uint64_t lastConsumedSequence() const {
        return consumedSeq_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get number of pending commands (approximate)
     */
    uint64_t pendingCount() const {
        uint64_t published = publishedSeq_.load(std::memory_order_acquire);
        uint64_t consumed = consumedSeq_.load(std::memory_order_acquire);
        
        return published > consumed ? published - consumed : 0;
    }
    
    /**
     * @brief Check if a sequence has been consumed
     */
    bool isConsumed(uint64_t sequence) const {
        return consumedSeq_.load(std::memory_order_acquire) >= sequence;
    }
    
    /**
     * @brief Wait for a sequence to be consumed (blocking)
     *
     * @param sequence Sequence to wait for
     * @param maxSpins Maximum spin iterations before returning
     * @return true if consumed, false if max spins reached
     */
    bool waitForConsumption(uint64_t sequence, uint32_t maxSpins = 1000000) const {
        for (uint32_t i = 0; i < maxSpins; ++i) {
            if (isConsumed(sequence)) {
                return true;
            }
            // Yield to avoid burning CPU
            if (i % 1000 == 0) {
                std::this_thread::yield();
            }
        }
        return false;
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return queue_.empty();
    }
    
    /**
     * @brief Get queue capacity
     */
    constexpr size_t capacity() const {
        return CAPACITY;
    }
    
    /**
     * @brief Get number of dropped commands (queue overflow)
     */
    uint64_t droppedCount() const {
        return droppedCount_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Reset dropped counter
     */
    void resetDroppedCount() {
        droppedCount_.store(0, std::memory_order_relaxed);
    }
    
private:
    /// Lock-free SPSC queue
    memory::SPSCQueue<VoxelCommand, CAPACITY> queue_;
    
    /// Next sequence number to assign (producer side)
    alignas(64) std::atomic<uint64_t> nextSequence_{1};
    
    /// Last published sequence (producer writes, consumer reads)
    alignas(64) std::atomic<uint64_t> publishedSeq_{0};
    
    /// Last consumed sequence (consumer writes, producer reads)
    alignas(64) std::atomic<uint64_t> consumedSeq_{0};
    
    /// Number of dropped commands due to queue overflow
    alignas(64) std::atomic<uint64_t> droppedCount_{0};
};

} // namespace voxel
} // namespace jupiter

