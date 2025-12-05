#pragma once

#ifdef _WIN32
    #ifdef MATH_EXPORTS
        #define MATH_API __declspec(dllexport)
    #elif defined(MATH_IMPORTS)
        #define MATH_API __declspec(dllimport)
    #else
        #define MATH_API
    #endif
#else
    #define MATH_API
#endif

#include <cmath>
#include <cstdint>

// GLM - OpenGL Mathematics library
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan uses depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace jupiter {
namespace math {

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

// Vector2 class
struct MATH_API Vector2 {
    float x, y;

    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const {
        return Vector2(x + other.x, y + other.y);
    }

    Vector2 operator-(const Vector2& other) const {
        return Vector2(x - other.x, y - other.y);
    }

    Vector2 operator*(float scalar) const {
        return Vector2(x * scalar, y * scalar);
    }

    Vector2 operator/(float scalar) const {
        return Vector2(x / scalar, y / scalar);
    }

    Vector2& operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vector2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    float length() const {
        return std::sqrt(x * x + y * y);
    }

    float lengthSquared() const {
        return x * x + y * y;
    }

    Vector2 normalized() const {
        float len = length();
        if (len > 0.0f) {
            return Vector2(x / len, y / len);
        }
        return Vector2(0.0f, 0.0f);
    }

    void normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
        }
    }

    static float dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    static Vector2 zero() { return Vector2(0.0f, 0.0f); }
    static Vector2 one() { return Vector2(1.0f, 1.0f); }
    static Vector2 up() { return Vector2(0.0f, 1.0f); }
    static Vector2 down() { return Vector2(0.0f, -1.0f); }
    static Vector2 left() { return Vector2(-1.0f, 0.0f); }
    static Vector2 right() { return Vector2(1.0f, 0.0f); }
};

// Vector3 class
struct MATH_API Vector3 {
    float x, y, z;

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    Vector3& operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vector3& operator-=(const Vector3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vector3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vector3& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float lengthSquared() const {
        return x * x + y * y + z * z;
    }

    Vector3 normalized() const {
        float len = length();
        if (len > 0.0f) {
            return Vector3(x / len, y / len, z / len);
        }
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    void normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }
    }

    static float dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vector3 cross(const Vector3& a, const Vector3& b) {
        return Vector3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    static Vector3 zero() { return Vector3(0.0f, 0.0f, 0.0f); }
    static Vector3 one() { return Vector3(1.0f, 1.0f, 1.0f); }
    static Vector3 up() { return Vector3(0.0f, 1.0f, 0.0f); }
    static Vector3 down() { return Vector3(0.0f, -1.0f, 0.0f); }
    static Vector3 left() { return Vector3(-1.0f, 0.0f, 0.0f); }
    static Vector3 right() { return Vector3(1.0f, 0.0f, 0.0f); }
    static Vector3 forward() { return Vector3(0.0f, 0.0f, 1.0f); }
    static Vector3 back() { return Vector3(0.0f, 0.0f, -1.0f); }
};

// Vector4 class
struct MATH_API Vector4 {
    float x, y, z, w;

    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vector4(const Vector3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    static Vector4 zero() { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }
    static Vector4 one() { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }
};

// ============================================================================
// Matrix4x4 - Wrapper around GLM mat4 for proper matrix math
// ============================================================================

/**
 * @brief 4x4 matrix using GLM under the hood
 *
 * Provides proper, well-tested matrix operations via GLM.
 * GLM is column-major (like OpenGL/Vulkan), so data layout is optimal.
 */
struct MATH_API Matrix4x4 {
    glm::mat4 m;

    // Constructors
    Matrix4x4() : m(1.0f) {}  // Identity matrix
    Matrix4x4(const glm::mat4& matrix) : m(matrix) {}
    Matrix4x4(float diagonal) : m(diagonal) {}

    // Access underlying GLM matrix
    glm::mat4& get() { return m; }
    const glm::mat4& get() const { return m; }

    // Access raw data (column-major)
    const float* data() const { return glm::value_ptr(m); }
    float* data() { return glm::value_ptr(m); }

    // Static factory methods
    static Matrix4x4 identity() {
        return Matrix4x4(glm::mat4(1.0f));
    }

    static Matrix4x4 translation(const Vector3& t) {
        return Matrix4x4(glm::translate(glm::mat4(1.0f), glm::vec3(t.x, t.y, t.z)));
    }

    static Matrix4x4 rotationX(float angle) {
        return Matrix4x4(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    }

    static Matrix4x4 rotationY(float angle) {
        return Matrix4x4(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    static Matrix4x4 rotationZ(float angle) {
        return Matrix4x4(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }

    static Matrix4x4 scale(const Vector3& s) {
        return Matrix4x4(glm::scale(glm::mat4(1.0f), glm::vec3(s.x, s.y, s.z)));
    }

    // Matrix multiplication
    Matrix4x4 operator*(const Matrix4x4& other) const {
        return Matrix4x4(m * other.m);
    }

    // Camera view matrix (look-at)
    static Matrix4x4 lookAt(const Vector3& eye, const Vector3& center, const Vector3& up) {
        return Matrix4x4(glm::lookAt(
            glm::vec3(eye.x, eye.y, eye.z),
            glm::vec3(center.x, center.y, center.z),
            glm::vec3(up.x, up.y, up.z)
        ));
    }

    // Perspective projection matrix (Vulkan-compatible with Y-flip)
    // Vulkan uses Y-down clip space, so we flip Y to match OpenGL conventions
    static Matrix4x4 perspective(float fovY, float aspect, float near, float far) {
        glm::mat4 proj = glm::perspective(fovY, aspect, near, far);
        // Flip Y for Vulkan coordinate system
        proj[1][1] *= -1.0f;
        return Matrix4x4(proj);
    }

    // Orthographic projection matrix (Vulkan-compatible with Y-flip)
    static Matrix4x4 orthographic(float left, float right, float bottom, float top, float near, float far) {
        glm::mat4 proj = glm::ortho(left, right, bottom, top, near, far);
        // Flip Y for Vulkan coordinate system
        proj[1][1] *= -1.0f;
        return Matrix4x4(proj);
    }
};

// Utility functions
MATH_API float degToRad(float degrees);
MATH_API float radToDeg(float radians);
MATH_API float clamp(float value, float min, float max);
MATH_API float lerp(float a, float b, float t);
MATH_API Vector2 lerp(const Vector2& a, const Vector2& b, float t);
MATH_API Vector3 lerp(const Vector3& a, const Vector3& b, float t);

} // namespace math
} // namespace jupiter
