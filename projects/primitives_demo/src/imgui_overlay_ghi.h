#pragma once

#include "rendering/ghi/ghi.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdint>
#include <vector>

namespace jupiter::rendering::demo {

// Simple ImGui overlay renderer implemented on top of GHI.
// Works on Metal/Vulkan/OpenGL because it uses SPIR-V + engine's fixed Vertex3D layout.
class ImGuiOverlayGHI {
public:
    ImGuiOverlayGHI() = default;
    ~ImGuiOverlayGHI();

    bool initialize(SDL_Window* window, uint32_t displayWidth, uint32_t displayHeight);
    void shutdown();

    void processEvent(const SDL_Event* event);
    void newFrame(float deltaTimeSeconds, uint32_t displayWidth, uint32_t displayHeight);
    void render();

    bool wantsCaptureKeyboard() const;
    bool wantsCaptureMouse() const;

private:
    struct GuiVertex {
        float px, py, a;   // mapped to location 0 vec3 (pos.xy + alpha)
        float r, g, b;     // mapped to location 1 vec3 (rgb)
        float u, v;        // mapped to location 2 vec2 (uv)
    };

    bool createDeviceObjects(uint32_t displayWidth, uint32_t displayHeight);
    void destroyDeviceObjects();

    bool uploadFontTexture();

    void ensureBuffers(size_t vertexCount, size_t indexCount);

    bool initialized_ = false;
    SDL_Window* window_ = nullptr;

    ghi::ShaderHandle shader_;
    ghi::BufferHandle ubo_;
    ghi::BufferHandle vbo_;
    ghi::BufferHandle ibo_;
    ghi::TextureHandle fontTexture_;

    // CPU staging (reused, avoid allocations per frame)
    std::vector<GuiVertex> vertices_;
    std::vector<uint16_t> indices_;

    uint32_t lastDisplayWidth_ = 0;
    uint32_t lastDisplayHeight_ = 0;
};

} // namespace jupiter::rendering::demo




