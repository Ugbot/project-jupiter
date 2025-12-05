#include "rendering/application.h"
#include "rendering/rendering.h"
#include "logging/logging.h"
#include "math/math.h"

#include <cmath>

using namespace jupiter::rendering;
using namespace jupiter::math;

/**
 * @brief Spinning Cube Demo Application
 *
 * Demonstrates:
 * - 3D cube geometry with 8 vertices and 12 triangular faces
 * - Rotation animation using time-based transformations
 * - Colored cube faces for visibility
 * - Simple 3D projection
 */
class SpinningCubeApplication : public Application {
public:
    SpinningCubeApplication()
        : Application("Spinning Cube Demo", 800, 600, false)
        , vertexBuffer_(nullptr)
        , indexBuffer_(nullptr)
        , indexCount_(0)
        , rotationAngle_(0.0f) {
    }

    ~SpinningCubeApplication() override = default;

protected:
    void onInit() override {
        LOG_INFO("CubeApp", "Initializing spinning cube demo");

        // Define cube vertices in 3D space
        // We'll project them to 2D for rendering
        // 8 vertices of a cube centered at origin
        Vertex3D cubeVertices3D[] = {
            // Front face (red)
            {{ -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }},  // 0: front-bottom-left
            {{  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }},  // 1: front-bottom-right
            {{  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }},  // 2: front-top-right
            {{ -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }},  // 3: front-top-left

            // Back face (cyan)
            {{ -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f }},  // 4: back-bottom-left
            {{  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f }},  // 5: back-bottom-right
            {{  0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f }},  // 6: back-top-right
            {{ -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f }},  // 7: back-top-left
        };

        // Store 3D vertices for rotation
        for (int i = 0; i < 8; i++) {
            vertices3D_[i] = cubeVertices3D[i];
        }

        // Define cube face indices (2 triangles per face = 6 faces * 2 * 3 = 36 indices)
        uint16_t cubeIndices[] = {
            // Front face
            0, 1, 2,  2, 3, 0,
            // Right face (green)
            1, 5, 6,  6, 2, 1,
            // Back face
            5, 4, 7,  7, 6, 5,
            // Left face (blue)
            4, 0, 3,  3, 7, 4,
            // Top face (yellow)
            3, 2, 6,  6, 7, 3,
            // Bottom face (magenta)
            4, 5, 1,  1, 0, 4
        };

        indexCount_ = 36;

        // Apply colors to different faces
        applyFaceColors();

        // Project 3D vertices to 2D for initial frame
        Vertex vertices2D[8];
        projectVerticesTo2D(vertices2D);

        // Create vertex buffer
        vertexBuffer_ = createVertexBuffer(vertices2D, 8);
        if (!vertexBuffer_) {
            LOG_ERROR("CubeApp", "Failed to create vertex buffer");
            return;
        }

        // Create index buffer
        indexBuffer_ = createIndexBuffer(cubeIndices, indexCount_);
        if (!indexBuffer_) {
            LOG_ERROR("CubeApp", "Failed to create index buffer");
            return;
        }

        // Load shaders
        if (!loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
            LOG_ERROR("CubeApp", "Failed to load shaders");
            return;
        }

        LOG_INFO("CubeApp", "Spinning cube initialized successfully");
        LOG_INFO("CubeApp", "Watch the cube spin! Close window to exit");
    }

    void onUpdate(float deltaTime) override {
        // Update rotation angle
        rotationAngle_ += deltaTime * 1.0f;  // 1 radian per second

        // Rotate vertices
        rotateVertices();

        // Project to 2D
        Vertex vertices2D[8];
        projectVerticesTo2D(vertices2D);

        // Update vertex buffer with new positions
        // Note: This is not efficient (updating every frame), but works for demo
        // In a real engine, we'd use push constants or uniform buffers
        updateVertexBuffer(vertices2D);
    }

    void onRender() override {
        bindPipeline();
        drawIndexed(vertexBuffer_, indexBuffer_, indexCount_);
    }

private:
    struct Vec3 {
        float x, y, z;
    };

    struct Vertex3D {
        Vec3 pos;
        Vec3 color;
    };

    vulkan::VulkanBuffer* vertexBuffer_;
    vulkan::VulkanBuffer* indexBuffer_;
    uint32_t indexCount_;
    float rotationAngle_;
    Vertex3D vertices3D_[8];
    Vertex3D originalVertices_[8];

    void applyFaceColors() {
        // Store original vertices
        for (int i = 0; i < 8; i++) {
            originalVertices_[i] = vertices3D_[i];
        }

        // Apply distinct colors to each face region
        // Front face - red (already set)
        // Right face vertices - green
        vertices3D_[1].color = Vec3{0.0f, 1.0f, 0.0f};
        vertices3D_[2].color = Vec3{0.0f, 1.0f, 0.0f};
        vertices3D_[5].color = Vec3{0.0f, 1.0f, 0.0f};
        vertices3D_[6].color = Vec3{0.0f, 1.0f, 0.0f};

        // Left face vertices - blue
        vertices3D_[0].color = Vec3{0.0f, 0.0f, 1.0f};
        vertices3D_[3].color = Vec3{0.0f, 0.0f, 1.0f};
        vertices3D_[4].color = Vec3{0.0f, 0.0f, 1.0f};
        vertices3D_[7].color = Vec3{0.0f, 0.0f, 1.0f};

        // Blend colors for shared vertices
        vertices3D_[0].color = Vec3{0.5f, 0.0f, 0.5f};  // Red + Blue
        vertices3D_[1].color = Vec3{0.5f, 0.5f, 0.0f};  // Red + Green
        vertices3D_[2].color = Vec3{0.5f, 0.5f, 0.0f};  // Red + Green
        vertices3D_[3].color = Vec3{0.5f, 0.0f, 0.5f};  // Red + Blue
        vertices3D_[4].color = Vec3{0.0f, 0.5f, 0.5f};  // Blue + Cyan
        vertices3D_[5].color = Vec3{0.0f, 0.5f, 0.5f};  // Green + Cyan
        vertices3D_[6].color = Vec3{0.0f, 0.5f, 0.5f};  // Green + Cyan
        vertices3D_[7].color = Vec3{0.0f, 0.5f, 0.5f};  // Blue + Cyan
    }

    void rotateVertices() {
        float cosY = std::cos(rotationAngle_);
        float sinY = std::sin(rotationAngle_);
        float cosX = std::cos(rotationAngle_ * 0.7f);  // Rotate slower on X axis
        float sinX = std::sin(rotationAngle_ * 0.7f);

        for (int i = 0; i < 8; i++) {
            // Get original position
            Vec3 pos = originalVertices_[i].pos;

            // Rotate around Y axis
            float x = pos.x * cosY - pos.z * sinY;
            float z = pos.x * sinY + pos.z * cosY;
            pos.x = x;
            pos.z = z;

            // Rotate around X axis
            float y = pos.y * cosX - pos.z * sinX;
            z = pos.y * sinX + pos.z * cosX;
            pos.y = y;
            pos.z = z;

            // Store rotated position
            vertices3D_[i].pos = pos;
        }
    }

    void projectVerticesTo2D(Vertex* outVertices) {
        // Simple perspective projection
        float fov = 1.5f;  // Field of view factor
        float distance = 3.0f;  // Camera distance

        for (int i = 0; i < 8; i++) {
            Vec3 pos = vertices3D_[i].pos;

            // Apply perspective projection
            float scale = distance / (distance + pos.z);
            float x = pos.x * scale * fov;
            float y = pos.y * scale * fov;

            // Convert to Vertex format (2D position + color)
            outVertices[i].pos[0] = x;
            outVertices[i].pos[1] = y;
            outVertices[i].color[0] = vertices3D_[i].color.x;
            outVertices[i].color[1] = vertices3D_[i].color.y;
            outVertices[i].color[2] = vertices3D_[i].color.z;
        }
    }

    void updateVertexBuffer(const Vertex* vertices) {
        // Recreate vertex buffer each frame (not efficient, but works for demo)
        // In production, use dynamic vertex buffers or push constants
        vertexBuffer_ = createVertexBuffer(vertices, 8);
    }
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "   Spinning Cube Demo");
    LOG_INFO("Main", "   Project Jupiter Engine");
    LOG_INFO("Main", "========================================");

    SpinningCubeApplication app;
    return app.run();
}
