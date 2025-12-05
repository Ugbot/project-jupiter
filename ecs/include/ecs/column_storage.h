#pragma once

/**
 * @file column_storage.h
 * @brief Typed columnar storage with SIMD-friendly alignment
 * 
 * Column<T> provides aligned, resizable storage for ECS component data.
 * Inspired by Apache Arrow's columnar arrays and Venus ECS.
 */

#include "types.h"
#include "span.h"
#include <vector>
#include <cstring>
#include <cassert>
#include <stdexcept>

// GLM for math types
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jupiter::ecs {

/**
 * @brief Type-erased column interface
 * 
 * Base interface for column storage, enabling uniform handling
 * of columns in the World without knowing concrete types.
 */
class IColumn {
public:
    virtual ~IColumn() = default;
    
    // Size management
    virtual void resize(size_t count) = 0;
    virtual void reserve(size_t capacity) = 0;
    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    virtual void clear() = 0;
    
    // Element info
    virtual size_t elementSize() const = 0;
    virtual size_t alignment() const = 0;
    
    // Copy operations
    virtual void copyFrom(const IColumn& src, size_t srcIdx, size_t dstIdx) = 0;
    virtual void copyRange(const IColumn& src, size_t srcStart, size_t dstStart, size_t count) = 0;
    
    // Raw data access
    virtual void* rawData() = 0;
    virtual const void* rawData() const = 0;
    
    // Clone for double-buffering
    virtual void cloneFrom(const IColumn& src) = 0;
};

/**
 * @brief Typed columnar storage with SIMD alignment
 * 
 * @tparam T Element type
 * @tparam Alignment Memory alignment (default: 64 bytes for AVX-512)
 */
template<typename T, size_t Alignment = SIMD_ALIGNMENT>
class Column : public IColumn {
public:
    using value_type = T;
    using allocator_type = AlignedAllocator<T, Alignment>;

    // ========================================================================
    // Construction
    // ========================================================================

    Column() = default;

    explicit Column(size_t initialCapacity) {
        reserve(initialCapacity);
    }

    Column(size_t count, const T& value) {
        resize(count);
        std::fill(data_.begin(), data_.end(), value);
    }

    // Move semantics
    Column(Column&&) noexcept = default;
    Column& operator=(Column&&) noexcept = default;

    // Copy semantics
    Column(const Column& other) : data_(other.data_) {}
    Column& operator=(const Column& other) {
        if (this != &other) {
            data_ = other.data_;
        }
        return *this;
    }

    // ========================================================================
    // IColumn Interface
    // ========================================================================

    void resize(size_t count) override {
        data_.resize(count);
    }

    void reserve(size_t capacity) override {
        data_.reserve(capacity);
    }

    size_t size() const override {
        return data_.size();
    }

    size_t capacity() const override {
        return data_.capacity();
    }

    void clear() override {
        data_.clear();
    }

    size_t elementSize() const override {
        return sizeof(T);
    }

    size_t alignment() const override {
        return Alignment;
    }

    void copyFrom(const IColumn& src, size_t srcIdx, size_t dstIdx) override {
        const auto& srcCol = static_cast<const Column<T, Alignment>&>(src);
        assert(srcIdx < srcCol.size() && dstIdx < size());
        data_[dstIdx] = srcCol.data_[srcIdx];
    }

    void copyRange(const IColumn& src, size_t srcStart, size_t dstStart, size_t count) override {
        const auto& srcCol = static_cast<const Column<T, Alignment>&>(src);
        assert(srcStart + count <= srcCol.size());
        assert(dstStart + count <= size());
        std::memcpy(&data_[dstStart], &srcCol.data_[srcStart], count * sizeof(T));
    }

    void* rawData() override {
        return data_.data();
    }

    const void* rawData() const override {
        return data_.data();
    }

    void cloneFrom(const IColumn& src) override {
        const auto& srcCol = static_cast<const Column<T, Alignment>&>(src);
        data_ = srcCol.data_;
    }

    // ========================================================================
    // Typed Access
    // ========================================================================

    T& operator[](size_t idx) {
        assert(idx < size());
        return data_[idx];
    }

    const T& operator[](size_t idx) const {
        assert(idx < size());
        return data_[idx];
    }

    T& at(size_t idx) {
        if (idx >= size()) {
            throw std::out_of_range("Column index out of range");
        }
        return data_[idx];
    }

    const T& at(size_t idx) const {
        if (idx >= size()) {
            throw std::out_of_range("Column index out of range");
        }
        return data_[idx];
    }

    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }

    // ========================================================================
    // Span Access
    // ========================================================================

    Span<T> span() {
        return Span<T>(data_.data(), data_.size());
    }

    Span<const T> span() const {
        return Span<const T>(data_.data(), data_.size());
    }

    Span<T> span(size_t offset, size_t count) {
        assert(offset + count <= size());
        return Span<T>(data_.data(), count, offset);
    }

    Span<const T> span(size_t offset, size_t count) const {
        assert(offset + count <= size());
        return Span<const T>(data_.data(), count, offset);
    }

    // ========================================================================
    // Iterators
    // ========================================================================

    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
    auto cbegin() const { return data_.cbegin(); }
    auto cend() const { return data_.cend(); }

    // ========================================================================
    // Modification
    // ========================================================================

    void push_back(const T& value) {
        data_.push_back(value);
    }

    void push_back(T&& value) {
        data_.push_back(std::move(value));
    }

    template<typename... Args>
    T& emplace_back(Args&&... args) {
        return data_.emplace_back(std::forward<Args>(args)...);
    }

    void pop_back() {
        data_.pop_back();
    }

    T& front() { return data_.front(); }
    const T& front() const { return data_.front(); }
    T& back() { return data_.back(); }
    const T& back() const { return data_.back(); }

    bool empty() const { return data_.empty(); }

    // ========================================================================
    // Bulk Operations
    // ========================================================================

    /**
     * @brief Fill all elements with a value
     */
    void fill(const T& value) {
        std::fill(data_.begin(), data_.end(), value);
    }

    /**
     * @brief Zero-fill (for POD types)
     */
    void zero() {
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memset(data_.data(), 0, data_.size() * sizeof(T));
        } else {
            fill(T{});
        }
    }

    /**
     * @brief Swap with another column
     */
    void swap(Column& other) noexcept {
        data_.swap(other.data_);
    }

private:
    std::vector<T, allocator_type> data_;
};

// ============================================================================
// Common Column Types
// ============================================================================

// Transform columns
using PositionColumn = Column<glm::vec3>;
using RotationColumn = Column<glm::quat>;
using ScaleColumn = Column<glm::vec3>;
using TransformColumn = Column<glm::mat4>;

// Physics columns
using VelocityColumn = Column<glm::vec3>;
using FloatColumn = Column<float>;
using BoolColumn = Column<uint8_t>;  // Use uint8_t for SIMD-friendly bool

// Entity metadata
using EntityIdColumn = Column<EntityId>;
using FlagsColumn = Column<EntityFlags>;
using IndexColumn = Column<uint32_t>;

} // namespace jupiter::ecs

