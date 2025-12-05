#pragma once

#include "math/math.h"

#ifdef _WIN32
    #ifdef RENDERING_EXPORTS
        #define RENDERING_API __declspec(dllexport)
    #elif defined(RENDERING_IMPORTS)
        #define RENDERING_API __declspec(dllimport)
    #else
        #define RENDERING_API
    #endif
#else
    #define RENDERING_API
#endif

namespace jupiter {
namespace rendering {

/**
 * @brief Camera base class
 *
 * Provides view and projection matrices for 3D rendering.
 * Following CLAUDE.md principles:
 * - No runtime allocations
 * - All matrices pre-allocated
 * - Update matrices only when camera changes
 */
class RENDERING_API Camera {
public:
    enum class ProjectionType {
        Perspective,
        Orthographic
    };

    Camera(ProjectionType type)
        : projectionType_(type)
        , position_(0.0f, 0.0f, 5.0f)
        , target_(0.0f, 0.0f, 0.0f)
        , up_(0.0f, 1.0f, 0.0f)
        , viewMatrix_(math::Matrix4x4::identity())
        , projectionMatrix_(math::Matrix4x4::identity())
        , viewProjectionMatrix_(math::Matrix4x4::identity())
        , viewDirty_(true)
        , projectionDirty_(true) {
    }

    virtual ~Camera() = default;

    // No copy (cameras are typically unique), allow move
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) = default;
    Camera& operator=(Camera&&) = default;

    // ========================================================================
    // Camera Transforms
    // ========================================================================

    /**
     * @brief Set camera position in world space
     */
    void setPosition(const math::Vector3& position) {
        position_ = position;
        viewDirty_ = true;
    }

    /**
     * @brief Set camera target (look-at point) in world space
     */
    void setTarget(const math::Vector3& target) {
        target_ = target;
        viewDirty_ = true;
    }

    /**
     * @brief Set camera up vector
     */
    void setUp(const math::Vector3& up) {
        up_ = up;
        viewDirty_ = true;
    }

    /**
     * @brief Set all camera transform parameters at once
     */
    void lookAt(const math::Vector3& position, const math::Vector3& target, const math::Vector3& up) {
        position_ = position;
        target_ = target;
        up_ = up;
        viewDirty_ = true;
    }

    // ========================================================================
    // Matrix Getters
    // ========================================================================

    /**
     * @brief Get view matrix (updates if dirty)
     */
    const math::Matrix4x4& getViewMatrix() const {
        if (viewDirty_) {
            updateViewMatrix();
        }
        return viewMatrix_;
    }

    /**
     * @brief Get projection matrix (updates if dirty)
     */
    const math::Matrix4x4& getProjectionMatrix() const {
        if (projectionDirty_) {
            updateProjectionMatrix();
        }
        return projectionMatrix_;
    }

    /**
     * @brief Get combined view-projection matrix (updates if dirty)
     */
    const math::Matrix4x4& getViewProjectionMatrix() const {
        if (viewDirty_ || projectionDirty_) {
            if (viewDirty_) updateViewMatrix();
            if (projectionDirty_) updateProjectionMatrix();
            viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
        }
        return viewProjectionMatrix_;
    }

    // ========================================================================
    // Getters
    // ========================================================================

    const math::Vector3& getPosition() const { return position_; }
    const math::Vector3& getTarget() const { return target_; }
    const math::Vector3& getUp() const { return up_; }
    ProjectionType getProjectionType() const { return projectionType_; }

protected:
    // Override this in derived classes to update projection matrix
    virtual void updateProjectionMatrix() const = 0;

    void updateViewMatrix() const {
        viewMatrix_ = math::Matrix4x4::lookAt(position_, target_, up_);
        viewDirty_ = false;
    }

    ProjectionType projectionType_;

    // Camera transform
    math::Vector3 position_;
    math::Vector3 target_;
    math::Vector3 up_;

    // Cached matrices (mutable for lazy evaluation)
    mutable math::Matrix4x4 viewMatrix_;
    mutable math::Matrix4x4 projectionMatrix_;
    mutable math::Matrix4x4 viewProjectionMatrix_;

    // Dirty flags (mutable for lazy evaluation)
    mutable bool viewDirty_;
    mutable bool projectionDirty_;
};

/**
 * @brief Perspective camera
 *
 * Provides perspective projection with field of view.
 * Suitable for 3D scenes with depth perception.
 */
class RENDERING_API PerspectiveCamera : public Camera {
public:
    /**
     * @brief Construct perspective camera
     * @param fovY Field of view in Y direction (radians)
     * @param aspect Aspect ratio (width / height)
     * @param near Near clipping plane
     * @param far Far clipping plane
     */
    PerspectiveCamera(float fovY = math::PI / 4.0f, float aspect = 16.0f / 9.0f,
                     float near = 0.1f, float far = 100.0f)
        : Camera(ProjectionType::Perspective)
        , fovY_(fovY)
        , aspect_(aspect)
        , near_(near)
        , far_(far) {
        updateProjectionMatrix();
    }

    // Setters (mark projection as dirty)
    void setFieldOfView(float fovY) { fovY_ = fovY; projectionDirty_ = true; }
    void setAspectRatio(float aspect) { aspect_ = aspect; projectionDirty_ = true; }
    void setNearPlane(float near) { near_ = near; projectionDirty_ = true; }
    void setFarPlane(float far) { far_ = far; projectionDirty_ = true; }

    void setPerspective(float fovY, float aspect, float near, float far) {
        fovY_ = fovY;
        aspect_ = aspect;
        near_ = near;
        far_ = far;
        projectionDirty_ = true;
    }

    // Getters
    float getFieldOfView() const { return fovY_; }
    float getAspectRatio() const { return aspect_; }
    float getNearPlane() const { return near_; }
    float getFarPlane() const { return far_; }

protected:
    void updateProjectionMatrix() const override {
        projectionMatrix_ = math::Matrix4x4::perspective(fovY_, aspect_, near_, far_);
        projectionDirty_ = false;
    }

private:
    float fovY_;    // Field of view in Y (radians)
    float aspect_;  // Aspect ratio (width/height)
    float near_;    // Near clipping plane
    float far_;     // Far clipping plane
};

/**
 * @brief Orthographic camera
 *
 * Provides orthographic (parallel) projection.
 * Suitable for 2D games, UI, or technical drawings.
 */
class RENDERING_API OrthographicCamera : public Camera {
public:
    /**
     * @brief Construct orthographic camera
     * @param left Left edge of view volume
     * @param right Right edge of view volume
     * @param bottom Bottom edge of view volume
     * @param top Top edge of view volume
     * @param near Near clipping plane
     * @param far Far clipping plane
     */
    OrthographicCamera(float left = -10.0f, float right = 10.0f,
                      float bottom = -10.0f, float top = 10.0f,
                      float near = 0.1f, float far = 100.0f)
        : Camera(ProjectionType::Orthographic)
        , left_(left)
        , right_(right)
        , bottom_(bottom)
        , top_(top)
        , near_(near)
        , far_(far) {
        updateProjectionMatrix();
    }

    // Setters (mark projection as dirty)
    void setLeft(float left) { left_ = left; projectionDirty_ = true; }
    void setRight(float right) { right_ = right; projectionDirty_ = true; }
    void setBottom(float bottom) { bottom_ = bottom; projectionDirty_ = true; }
    void setTop(float top) { top_ = top; projectionDirty_ = true; }
    void setNearPlane(float near) { near_ = near; projectionDirty_ = true; }
    void setFarPlane(float far) { far_ = far; projectionDirty_ = true; }

    void setOrthographic(float left, float right, float bottom, float top, float near, float far) {
        left_ = left;
        right_ = right;
        bottom_ = bottom;
        top_ = top;
        near_ = near;
        far_ = far;
        projectionDirty_ = true;
    }

    // Getters
    float getLeft() const { return left_; }
    float getRight() const { return right_; }
    float getBottom() const { return bottom_; }
    float getTop() const { return top_; }
    float getNearPlane() const { return near_; }
    float getFarPlane() const { return far_; }

protected:
    void updateProjectionMatrix() const override {
        projectionMatrix_ = math::Matrix4x4::orthographic(left_, right_, bottom_, top_, near_, far_);
        projectionDirty_ = false;
    }

private:
    float left_, right_;      // Horizontal bounds
    float bottom_, top_;      // Vertical bounds
    float near_, far_;        // Depth bounds
};

} // namespace rendering
} // namespace jupiter
