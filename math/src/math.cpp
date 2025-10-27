#include "math/math.h"

namespace jupiter {
namespace math {

float degToRad(float degrees) {
    return degrees * DEG_TO_RAD;
}

float radToDeg(float radians) {
    return radians * RAD_TO_DEG;
}

float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Vector2 lerp(const Vector2& a, const Vector2& b, float t) {
    return Vector2(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t
    );
}

Vector3 lerp(const Vector3& a, const Vector3& b, float t) {
    return Vector3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

} // namespace math
} // namespace jupiter
