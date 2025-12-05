/**
 * @file camera_ubo.glsl
 * @brief Camera uniform buffer object structure
 * 
 * Defines the camera UBO used across all shaders that need
 * view/projection matrices and camera position.
 * 
 * Usage: #include <includes/camera_ubo.glsl>
 */

#ifndef CAMERA_UBO_GLSL
#define CAMERA_UBO_GLSL

/**
 * @brief Camera uniform buffer object
 * 
 * Contains view and projection matrices plus camera position
 * for view-dependent calculations.
 */
struct CameraUBO {
    mat4 view;          // View matrix (world to view space)
    mat4 projection;    // Projection matrix (view to clip space)
    vec4 position;      // Camera world position (.xyz), .w unused
    float cameraNear;   // Near clip plane
    float cameraFar;    // Far clip plane
};

#endif // CAMERA_UBO_GLSL

