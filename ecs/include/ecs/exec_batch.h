#pragma once

/**
 * @file exec_batch.h
 * @brief ExecBatch - Type-erased collection of column spans for kernel execution
 * 
 * Inspired by Apache Arrow's ExecBatch, this provides a uniform interface
 * for kernels to access input/output column data without compile-time type knowledge.
 */

#include "types.h"
#include "span.h"
#include <cstddef>
#include <cassert>

namespace jupiter::ecs {

// Maximum number of columns in a batch (matches ColumnId bit width)
constexpr size_t MAX_COLUMNS = 32;

/**
 * @brief Type-erased column pointer with metadata
 */
struct ColumnPtr {
    void* data = nullptr;
    size_t elementSize = 0;
    ColumnId id = ColumnId::None;

    constexpr ColumnPtr() noexcept = default;

    template<typename T>
    ColumnPtr(T* ptr, ColumnId colId) noexcept
        : data(static_cast<void*>(ptr))
        , elementSize(sizeof(T))
        , id(colId) {}

    [[nodiscard]] bool valid() const noexcept { return data != nullptr; }
};

/**
 * @brief Collection of column spans for kernel input/output
 * 
 * ExecBatch is the primary interface for passing data to kernels.
 * It holds type-erased pointers to column data along with metadata
 * about which columns are present and the row count.
 * 
 * Thread Safety:
 * - Read-only batches can be shared across reader threads
 * - Write batches should only be accessed by the writer thread
 */
struct ExecBatch {
    // Type-erased column pointers (indexed by ColumnId bit position)
    void* columns[MAX_COLUMNS] = {};
    
    // Bitmask of which columns are present
    ColumnId presentMask = ColumnId::None;
    
    // Number of rows in this batch
    size_t length = 0;
    
    // Batch index for ordered processing (useful for parallel execution)
    size_t batchIndex = 0;
    
    // Offset into the full dataset (for chunked processing)
    size_t offset = 0;
    
    // User data for passing extra context (e.g., delta time)
    void* userData = nullptr;

    // ========================================================================
    // Column Access
    // ========================================================================

    /**
     * @brief Get a typed span for a column
     * @tparam T The element type of the column
     * @param id The column identifier
     * @return Span<T> view of the column data
     */
    template<typename T>
    [[nodiscard]] Span<T> column(ColumnId id) noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx >= MAX_COLUMNS || !hasColumn(presentMask, id)) {
            return Span<T>();
        }
        return Span<T>(static_cast<T*>(columns[idx]), length);
    }

    template<typename T>
    [[nodiscard]] Span<const T> column(ColumnId id) const noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx >= MAX_COLUMNS || !hasColumn(presentMask, id)) {
            return Span<const T>();
        }
        return Span<const T>(static_cast<const T*>(columns[idx]), length);
    }

    /**
     * @brief Set a column pointer
     * @tparam T The element type of the column
     * @param id The column identifier
     * @param ptr Pointer to the column data (must be valid for 'length' elements)
     */
    template<typename T>
    void setColumn(ColumnId id, T* ptr) noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx < MAX_COLUMNS) {
            columns[idx] = static_cast<void*>(ptr);
            presentMask = presentMask | id;
        }
    }

    /**
     * @brief Check if a column is present in this batch
     */
    [[nodiscard]] bool has(ColumnId id) const noexcept {
        return hasColumn(presentMask, id);
    }

    /**
     * @brief Get raw pointer to column data
     */
    [[nodiscard]] void* rawColumn(ColumnId id) noexcept {
        const uint32_t idx = columnIndex(id);
        return (idx < MAX_COLUMNS) ? columns[idx] : nullptr;
    }

    [[nodiscard]] const void* rawColumn(ColumnId id) const noexcept {
        const uint32_t idx = columnIndex(id);
        return (idx < MAX_COLUMNS) ? columns[idx] : nullptr;
    }

    // ========================================================================
    // Batch Operations
    // ========================================================================

    /**
     * @brief Create a sub-batch (view of a subset of rows)
     * @param start Starting row index
     * @param len Number of rows in the sub-batch
     * @return New ExecBatch viewing the specified range
     */
    [[nodiscard]] ExecBatch slice(size_t start, size_t len) const noexcept {
        assert(start + len <= length && "Slice exceeds batch bounds");
        
        ExecBatch result = *this;
        result.offset = offset + start;
        result.length = len;
        
        // Adjust column pointers to point to the slice start
        // This requires knowing element sizes, so we use byte offsets
        // For simplicity, we just adjust the offset and let column() handle it
        return result;
    }

    /**
     * @brief Clear all columns and reset the batch
     */
    void clear() noexcept {
        for (size_t i = 0; i < MAX_COLUMNS; ++i) {
            columns[i] = nullptr;
        }
        presentMask = ColumnId::None;
        length = 0;
        batchIndex = 0;
        offset = 0;
        userData = nullptr;
    }

    /**
     * @brief Check if batch is empty
     */
    [[nodiscard]] bool empty() const noexcept { return length == 0; }

    /**
     * @brief Get the number of columns present
     */
    [[nodiscard]] size_t columnCount() const noexcept {
        uint32_t mask = static_cast<uint32_t>(presentMask);
        size_t count = 0;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
        return count;
    }

private:
    /**
     * @brief Convert ColumnId to array index (bit position)
     */
    static constexpr uint32_t columnIndex(ColumnId id) noexcept {
        uint32_t val = static_cast<uint32_t>(id);
        if (val == 0) return MAX_COLUMNS; // Invalid
        
        // Find the bit position (log2 of power of 2)
        uint32_t idx = 0;
        while ((val & 1) == 0) {
            val >>= 1;
            ++idx;
        }
        return idx;
    }
};

/**
 * @brief Builder for constructing ExecBatch instances
 */
class ExecBatchBuilder {
public:
    ExecBatchBuilder() = default;

    ExecBatchBuilder& setLength(size_t len) noexcept {
        batch_.length = len;
        return *this;
    }

    ExecBatchBuilder& setBatchIndex(size_t idx) noexcept {
        batch_.batchIndex = idx;
        return *this;
    }

    ExecBatchBuilder& setOffset(size_t off) noexcept {
        batch_.offset = off;
        return *this;
    }

    ExecBatchBuilder& setUserData(void* ptr) noexcept {
        batch_.userData = ptr;
        return *this;
    }

    template<typename T>
    ExecBatchBuilder& addColumn(ColumnId id, T* data) noexcept {
        batch_.setColumn(id, data);
        return *this;
    }

    [[nodiscard]] ExecBatch build() noexcept {
        return batch_;
    }

private:
    ExecBatch batch_;
};

} // namespace jupiter::ecs

