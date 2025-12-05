#pragma once

/**
 * @file span.h
 * @brief Lightweight, non-owning view of contiguous data (Arrow ArraySpan-inspired)
 * 
 * Span<T> provides zero-copy access to column data for kernel execution.
 * It is stack-allocated and designed for efficient batch processing.
 */

#include "types.h"
#include <cstddef>
#include <cassert>
#include <type_traits>

namespace jupiter::ecs {

/**
 * @brief Non-owning, lightweight view of contiguous typed data
 * 
 * Similar to Arrow's ArraySpan, this provides zero-copy access to column data.
 * All operations are bounds-checked in debug builds.
 */
template<typename T>
struct Span {
    T* data = nullptr;
    size_t length = 0;
    size_t offset = 0;  // For slicing without pointer arithmetic

    // Default constructor
    constexpr Span() noexcept = default;

    // Construct from raw pointer and length
    constexpr Span(T* ptr, size_t len) noexcept 
        : data(ptr), length(len), offset(0) {}

    // Construct from raw pointer, length, and offset
    constexpr Span(T* ptr, size_t len, size_t off) noexcept 
        : data(ptr), length(len), offset(off) {}

    // Construct from container with data() and size() (e.g., std::vector)
    template<typename Container, 
             typename = std::enable_if_t<
                 std::is_same_v<std::remove_cv_t<T>, 
                               std::remove_cv_t<typename std::remove_pointer_t<
                                   decltype(std::declval<Container&>().data())>>>>>
    Span(Container& container) noexcept 
        : data(container.data()), length(container.size()), offset(0) {}

    // Element access with bounds checking in debug
    T& operator[](size_t i) noexcept {
        assert(i < length && "Span index out of bounds");
        return data[offset + i];
    }

    const T& operator[](size_t i) const noexcept {
        assert(i < length && "Span index out of bounds");
        return data[offset + i];
    }

    // Create a sub-span (slice)
    [[nodiscard]] Span<T> slice(size_t start, size_t len) const noexcept {
        assert(start + len <= length && "Slice exceeds span bounds");
        return Span<T>(data, len, offset + start);
    }

    // Create a sub-span from start to end
    [[nodiscard]] Span<T> slice(size_t start) const noexcept {
        assert(start <= length && "Slice start exceeds span bounds");
        return Span<T>(data, length - start, offset + start);
    }

    // Iterator support for range-based for loops
    T* begin() noexcept { return data + offset; }
    T* end() noexcept { return data + offset + length; }
    const T* begin() const noexcept { return data + offset; }
    const T* end() const noexcept { return data + offset + length; }
    const T* cbegin() const noexcept { return data + offset; }
    const T* cend() const noexcept { return data + offset + length; }

    // Direct pointer access (for SIMD operations)
    T* ptr() noexcept { return data + offset; }
    const T* ptr() const noexcept { return data + offset; }

    // Size queries
    [[nodiscard]] size_t size() const noexcept { return length; }
    [[nodiscard]] bool empty() const noexcept { return length == 0; }

    // Check if span is valid (non-null and non-empty)
    [[nodiscard]] bool valid() const noexcept { return data != nullptr && length > 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    // Front/back access
    T& front() noexcept { assert(!empty()); return (*this)[0]; }
    const T& front() const noexcept { assert(!empty()); return (*this)[0]; }
    T& back() noexcept { assert(!empty()); return (*this)[length - 1]; }
    const T& back() const noexcept { assert(!empty()); return (*this)[length - 1]; }

    // Convert to const span
    [[nodiscard]] Span<const T> asConst() const noexcept {
        return Span<const T>(data, length, offset);
    }
};

// Deduction guides
template<typename T>
Span(T*, size_t) -> Span<T>;

template<typename T>
Span(T*, size_t, size_t) -> Span<T>;

template<typename Container>
Span(Container&) -> Span<typename Container::value_type>;

/**
 * @brief Create a span from a raw pointer and count
 */
template<typename T>
[[nodiscard]] constexpr Span<T> makeSpan(T* ptr, size_t count) noexcept {
    return Span<T>(ptr, count);
}

/**
 * @brief Create a span from a container
 */
template<typename Container>
[[nodiscard]] auto makeSpan(Container& c) noexcept {
    return Span(c.data(), c.size());
}

/**
 * @brief Create a const span from a container
 */
template<typename Container>
[[nodiscard]] auto makeConstSpan(const Container& c) noexcept {
    return Span<const typename Container::value_type>(c.data(), c.size());
}

} // namespace jupiter::ecs

