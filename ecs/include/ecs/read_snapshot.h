#pragma once

/**
 * @file read_snapshot.h
 * @brief ReadSnapshot - Immutable view for reader threads
 * 
 * Provides stable, read-only access to entity data for multiple
 * concurrent reader threads (render, audio, networking, AI).
 */

#include "types.h"
#include "span.h"
#include "exec_batch.h"
#include "entity_buffer.h"

namespace jupiter::ecs {

// Forward declaration
class World;

/**
 * @brief Stable, immutable snapshot for reader threads
 * 
 * ReadSnapshot provides thread-safe read access to entity data.
 * Multiple reader threads can hold snapshots simultaneously while
 * the writer thread mutates the other buffer.
 * 
 * Validity:
 * - A snapshot remains valid until the next World::swap()
 * - After swap(), the snapshot references stale data
 * - Use isValid() to check before accessing after potential swap
 * 
 * Thread Safety:
 * - All read operations are thread-safe
 * - Multiple readers can access the same snapshot concurrently
 */
class ReadSnapshot {
public:
    /**
     * @brief Construct a snapshot from a buffer
     */
    ReadSnapshot(const EntityBuffer* buffer, uint64_t gen) noexcept
        : buffer_(buffer), generation_(gen) {}

    /**
     * @brief Default constructor (invalid snapshot)
     */
    ReadSnapshot() noexcept : buffer_(nullptr), generation_(0) {}

    // ========================================================================
    // Validity
    // ========================================================================

    /**
     * @brief Check if this snapshot is still valid
     * @param world The world to check against
     * @return true if snapshot hasn't been invalidated by swap()
     */
    [[nodiscard]] bool isValid(const World& world) const noexcept;

    /**
     * @brief Check if snapshot has valid data
     */
    [[nodiscard]] bool valid() const noexcept { 
        return buffer_ != nullptr; 
    }

    [[nodiscard]] explicit operator bool() const noexcept { 
        return valid(); 
    }

    // ========================================================================
    // Metadata
    // ========================================================================

    /**
     * @brief Get the generation when this snapshot was taken
     */
    [[nodiscard]] uint64_t generation() const noexcept { 
        return generation_; 
    }

    /**
     * @brief Get the number of entities in this snapshot
     */
    [[nodiscard]] size_t entityCount() const noexcept { 
        return buffer_ ? buffer_->count : 0; 
    }

    // ========================================================================
    // Column Access (read-only)
    // ========================================================================

    // Entity metadata
    [[nodiscard]] Span<const EntityId> ids() const noexcept {
        return buffer_ ? buffer_->ids.span() : Span<const EntityId>();
    }

    [[nodiscard]] Span<const EntityFlags> flags() const noexcept {
        return buffer_ ? buffer_->flags.span() : Span<const EntityFlags>();
    }

    // Transform
    [[nodiscard]] Span<const glm::vec3> positions() const noexcept {
        return buffer_ ? buffer_->positions.span() : Span<const glm::vec3>();
    }

    [[nodiscard]] Span<const glm::quat> rotations() const noexcept {
        return buffer_ ? buffer_->rotations.span() : Span<const glm::quat>();
    }

    [[nodiscard]] Span<const glm::vec3> scales() const noexcept {
        return buffer_ ? buffer_->scales.span() : Span<const glm::vec3>();
    }

    [[nodiscard]] Span<const glm::mat4> transforms() const noexcept {
        return buffer_ ? buffer_->transforms.span() : Span<const glm::mat4>();
    }

    // Physics
    [[nodiscard]] Span<const glm::vec3> linearVelocities() const noexcept {
        return buffer_ ? buffer_->linearVelocities.span() : Span<const glm::vec3>();
    }

    [[nodiscard]] Span<const glm::vec3> angularVelocities() const noexcept {
        return buffer_ ? buffer_->angularVelocities.span() : Span<const glm::vec3>();
    }

    [[nodiscard]] Span<const float> massesInverse() const noexcept {
        return buffer_ ? buffer_->massesInverse.span() : Span<const float>();
    }

    // Rendering
    [[nodiscard]] Span<const uint32_t> materialIds() const noexcept {
        return buffer_ ? buffer_->materialIds.span() : Span<const uint32_t>();
    }

    [[nodiscard]] Span<const uint32_t> meshIds() const noexcept {
        return buffer_ ? buffer_->meshIds.span() : Span<const uint32_t>();
    }

    [[nodiscard]] Span<const uint32_t> visibility() const noexcept {
        return buffer_ ? buffer_->visibility.span() : Span<const uint32_t>();
    }

    [[nodiscard]] Span<const simd::AABB> aabbs() const noexcept {
        return buffer_ ? buffer_->aabbs.span() : Span<const simd::AABB>();
    }

    // ========================================================================
    // Batch Access
    // ========================================================================

    /**
     * @brief Create an ExecBatch for kernel execution
     * @param offset Starting entity index
     * @param length Number of entities in batch
     */
    [[nodiscard]] ExecBatch makeBatch(size_t offset, size_t length) const noexcept {
        if (!buffer_ || offset >= buffer_->count) {
            return ExecBatch{};
        }

        length = std::min(length, static_cast<size_t>(buffer_->count) - offset);

        ExecBatch batch;
        batch.length = length;
        batch.offset = offset;

        // Set up column pointers (const cast is safe - batch is read-only)
        batch.setColumn(ColumnId::Ids, const_cast<EntityId*>(buffer_->ids.data() + offset));
        batch.setColumn(ColumnId::Flags, const_cast<EntityFlags*>(buffer_->flags.data() + offset));
        batch.setColumn(ColumnId::Positions, const_cast<glm::vec3*>(buffer_->positions.data() + offset));
        batch.setColumn(ColumnId::Rotations, const_cast<glm::quat*>(buffer_->rotations.data() + offset));
        batch.setColumn(ColumnId::Scales, const_cast<glm::vec3*>(buffer_->scales.data() + offset));
        batch.setColumn(ColumnId::Transforms, const_cast<glm::mat4*>(buffer_->transforms.data() + offset));
        batch.setColumn(ColumnId::LinearVelocity, const_cast<glm::vec3*>(buffer_->linearVelocities.data() + offset));
        batch.setColumn(ColumnId::AngularVelocity, const_cast<glm::vec3*>(buffer_->angularVelocities.data() + offset));
        batch.setColumn(ColumnId::MaterialIds, const_cast<uint32_t*>(buffer_->materialIds.data() + offset));
        batch.setColumn(ColumnId::MeshIds, const_cast<uint32_t*>(buffer_->meshIds.data() + offset));
        batch.setColumn(ColumnId::Visibility, const_cast<uint32_t*>(buffer_->visibility.data() + offset));
        batch.setColumn(ColumnId::AABBs, const_cast<simd::AABB*>(buffer_->aabbs.data() + offset));

        return batch;
    }

    /**
     * @brief Get raw buffer pointer (for advanced use)
     */
    [[nodiscard]] const EntityBuffer* buffer() const noexcept { 
        return buffer_; 
    }

private:
    const EntityBuffer* buffer_;
    uint64_t generation_;
};

} // namespace jupiter::ecs

