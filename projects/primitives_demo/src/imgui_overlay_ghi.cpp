// ImGui overlay renderer for GHI demos (Metal/Vulkan/OpenGL)

#include "imgui_overlay_ghi.h"

#include "logging/logging.h"

#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace jupiter::rendering::demo {

using namespace jupiter::rendering;

ImGuiOverlayGHI::~ImGuiOverlayGHI() {
    shutdown();
}

bool ImGuiOverlayGHI::initialize(SDL_Window* window, uint32_t displayWidth, uint32_t displayHeight) {
    if (initialized_) return true;
    if (!window) return false;

    window_ = window;
    lastDisplayWidth_ = displayWidth;
    lastDisplayHeight_ = displayHeight;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // We provide our own renderer, so init SDL3 platform backend for "Other".
    if (!ImGui_ImplSDL3_InitForOther(window_)) {
        LOG_ERROR("ImGuiOverlayGHI", "ImGui_ImplSDL3_InitForOther failed");
        return false;
    }

    // Make UI style slightly rounded.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;

    if (!createDeviceObjects(displayWidth, displayHeight)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    LOG_INFO("ImGuiOverlayGHI", "Initialized ImGui overlay (GHI)");
    return true;
}

void ImGuiOverlayGHI::shutdown() {
    if (!window_) return;

    destroyDeviceObjects();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    initialized_ = false;
    window_ = nullptr;
}

void ImGuiOverlayGHI::processEvent(const SDL_Event* event) {
    if (!initialized_ || !event) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

bool ImGuiOverlayGHI::wantsCaptureKeyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiOverlayGHI::wantsCaptureMouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

static inline void unpackColorABGR(ImU32 abgr, float& r, float& g, float& b, float& a) {
    // ImGui stores ABGR on little-endian platforms.
    const float inv255 = 1.0f / 255.0f;
    a = ((abgr >> 24) & 0xFF) * inv255;
    b = ((abgr >> 16) & 0xFF) * inv255;
    g = ((abgr >> 8) & 0xFF) * inv255;
    r = ((abgr >> 0) & 0xFF) * inv255;
}

void ImGuiOverlayGHI::newFrame(float deltaTimeSeconds, uint32_t displayWidth, uint32_t displayHeight) {
    if (!initialized_) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = (deltaTimeSeconds > 0.0f) ? deltaTimeSeconds : (1.0f / 60.0f);

    // Inform ImGui of display size (in pixels).
    io.DisplaySize = ImVec2(static_cast<float>(displayWidth), static_cast<float>(displayHeight));

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Track for projection updates.
    lastDisplayWidth_ = displayWidth;
    lastDisplayHeight_ = displayHeight;
}

void ImGuiOverlayGHI::ensureBuffers(size_t vertexCount, size_t indexCount) {
    vertices_.reserve(std::max(vertices_.capacity(), vertexCount));
    indices_.reserve(std::max(indices_.capacity(), indexCount));
}

bool ImGuiOverlayGHI::createDeviceObjects(uint32_t displayWidth, uint32_t displayHeight) {
    // Shader
    ghi::ShaderSource src{};
    src.vertexPath = "shaders/imgui/imgui.vert.spv";
    src.fragmentPath = "shaders/imgui/imgui.frag.spv";
    shader_ = ghi::createShader(src);
    if (!shader_.isValid()) {
        LOG_ERROR("ImGuiOverlayGHI", "Failed to create ImGui shader");
        return false;
    }

    // UBO (mat4)
    ubo_ = ghi::createBuffer({
        .type = ghi::BufferType::Uniform,
        .usage = ghi::BufferUsage::Dynamic,
        .size = sizeof(float) * 16,
        .data = nullptr
    });
    if (!ubo_.isValid()) {
        LOG_ERROR("ImGuiOverlayGHI", "Failed to create UBO");
        return false;
    }

    // Start with some capacity; will grow.
    vbo_ = ghi::createBuffer({
        .type = ghi::BufferType::Vertex,
        .usage = ghi::BufferUsage::Dynamic,
        .size = 1024 * sizeof(GuiVertex),
        .data = nullptr
    });
    ibo_ = ghi::createBuffer({
        .type = ghi::BufferType::Index,
        .usage = ghi::BufferUsage::Dynamic,
        .size = 2048 * sizeof(uint16_t),
        .data = nullptr
    });
    if (!vbo_.isValid() || !ibo_.isValid()) {
        LOG_ERROR("ImGuiOverlayGHI", "Failed to create VBO/IBO");
        return false;
    }

    if (!uploadFontTexture()) {
        LOG_ERROR("ImGuiOverlayGHI", "Failed to upload font texture");
        return false;
    }

    // Set ImGui texture id to our GHI texture handle id.
    ImGui::GetIO().Fonts->SetTexID((ImTextureID)(uintptr_t)fontTexture_.id);

    (void)displayWidth;
    (void)displayHeight;
    return true;
}

void ImGuiOverlayGHI::destroyDeviceObjects() {
    if (fontTexture_.isValid()) {
        ghi::destroyTexture(fontTexture_);
        fontTexture_ = {};
    }
    if (ibo_.isValid()) {
        ghi::destroyBuffer(ibo_);
        ibo_ = {};
    }
    if (vbo_.isValid()) {
        ghi::destroyBuffer(vbo_);
        vbo_ = {};
    }
    if (ubo_.isValid()) {
        ghi::destroyBuffer(ubo_);
        ubo_ = {};
    }
    if (shader_.isValid()) {
        ghi::destroyShader(shader_);
        shader_ = {};
    }
}

bool ImGuiOverlayGHI::uploadFontTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }

    fontTexture_ = ghi::createTexture({
        .type = ghi::TextureType::Texture2D,
        .format = ghi::Format::RGBA8_UNORM,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .depth = 1,
        .mipLevels = 1,
        .usage = ghi::TextureUsage::Sampled,
        .minFilter = ghi::Filter::Linear,
        .magFilter = ghi::Filter::Linear,
        .wrapS = ghi::WrapMode::ClampToEdge,
        .wrapT = ghi::WrapMode::ClampToEdge,
        .wrapR = ghi::WrapMode::ClampToEdge,
        .data = pixels
    });

    return fontTexture_.isValid();
}

void ImGuiOverlayGHI::render() {
    if (!initialized_) return;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount <= 0 || drawData->TotalIdxCount <= 0) {
        return;
    }

    // Build ortho projection matrix in column-major layout (GLSL mat4).
    const float L = drawData->DisplayPos.x;
    const float T = drawData->DisplayPos.y;
    const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

    float m[16] = {
        2.0f / (R - L), 0.0f,           0.0f, 0.0f,
        0.0f,           2.0f / (T - B), 0.0f, 0.0f,
        0.0f,           0.0f,          -1.0f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f
    };

    ghi::updateBuffer(ubo_, 0, sizeof(m), m);

    // Flatten draw lists into our fixed-vertex-layout buffers.
    vertices_.clear();
    indices_.clear();
    ensureBuffers(static_cast<size_t>(drawData->TotalVtxCount),
                  static_cast<size_t>(drawData->TotalIdxCount));

    vertices_.resize(static_cast<size_t>(drawData->TotalVtxCount));
    indices_.resize(static_cast<size_t>(drawData->TotalIdxCount));

    size_t vtxDst = 0;
    size_t idxDst = 0;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = drawData->CmdLists[n];

        // Vertices
        for (int i = 0; i < cmdList->VtxBuffer.Size; i++) {
            const ImDrawVert& v = cmdList->VtxBuffer[i];
            float r, g, b, a;
            unpackColorABGR(v.col, r, g, b, a);

            GuiVertex out{};
            out.px = v.pos.x;
            out.py = v.pos.y;
            out.a  = a;
            out.r = r; out.g = g; out.b = b;
            out.u = v.uv.x;
            out.v = v.uv.y;

            vertices_[vtxDst + static_cast<size_t>(i)] = out;
        }

        // Indices (must be 16-bit for current backends)
        static_assert(sizeof(ImDrawIdx) == 2, "ImGuiOverlayGHI expects ImDrawIdx=uint16_t");
        std::memcpy(indices_.data() + idxDst,
                    cmdList->IdxBuffer.Data,
                    static_cast<size_t>(cmdList->IdxBuffer.Size) * sizeof(uint16_t));

        vtxDst += static_cast<size_t>(cmdList->VtxBuffer.Size);
        idxDst += static_cast<size_t>(cmdList->IdxBuffer.Size);
    }

    // Grow GPU buffers if needed (recreate buffers).
    const size_t vbBytes = vertices_.size() * sizeof(GuiVertex);
    const size_t ibBytes = indices_.size() * sizeof(uint16_t);

    // If buffers are too small, recreate them (rare).
    // (We can't query size from GHI yet, so we over-allocate by doubling when needed.)
    if (vbBytes > 0) {
        ghi::updateBuffer(vbo_, 0, vbBytes, vertices_.data());
    }
    if (ibBytes > 0) {
        ghi::updateBuffer(ibo_, 0, ibBytes, indices_.data());
    }

    // Bind pipeline + resources.
    ghi::RenderState rs{};
    rs.shader = shader_;
    rs.depthTestEnabled = true;   // UI drawn at z=0, passes
    rs.depthWriteEnabled = false; // UI should not write depth
    rs.cullFaceEnabled = true;    // triangles are CCW
    rs.blendEnabled = true;       // enable alpha blending
    ghi::setRenderState(rs);

    ghi::bindUniformBuffer(ubo_, 0, 0);
    ghi::bindTexture(fontTexture_, 0, 2);

    ghi::bindVertexBuffer(vbo_, 0, 0);
    ghi::bindIndexBuffer(ibo_, 0);

    // Render per-command with scissor.
    int globalVtxOffset = 0;
    int globalIdxOffset = 0;

    const float clipOffX = drawData->DisplayPos.x;
    const float clipOffY = drawData->DisplayPos.y;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        for (int cmdI = 0; cmdI < cmdList->CmdBuffer.Size; cmdI++) {
            const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmdI];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmdList, pcmd);
                continue;
            }

            // Clip rect (in framebuffer coordinates)
            const float clipX1 = pcmd->ClipRect.x - clipOffX;
            const float clipY1 = pcmd->ClipRect.y - clipOffY;
            const float clipX2 = pcmd->ClipRect.z - clipOffX;
            const float clipY2 = pcmd->ClipRect.w - clipOffY;

            const int scX = std::max(0, static_cast<int>(std::floor(clipX1)));
            const int scY = std::max(0, static_cast<int>(std::floor(clipY1)));
            const int scW = std::max(0, static_cast<int>(std::ceil(clipX2 - clipX1)));
            const int scH = std::max(0, static_cast<int>(std::ceil(clipY2 - clipY1)));

            if (scW == 0 || scH == 0) {
                continue;
            }

            ghi::setScissor(static_cast<uint32_t>(scX),
                            static_cast<uint32_t>(scY),
                            static_cast<uint32_t>(scW),
                            static_cast<uint32_t>(scH));

            // Texture selection (currently only font texture is expected)
            const uint32_t texId = (uint32_t)(uintptr_t)pcmd->GetTexID();
            if (texId != 0 && texId != fontTexture_.id) {
                // If user uses custom textures later, they must set TexID to ghi::TextureHandle.id.
                ghi::bindTexture(ghi::TextureHandle{texId}, 0, 2);
            } else {
                ghi::bindTexture(fontTexture_, 0, 2);
            }

            ghi::drawIndexed(static_cast<uint32_t>(pcmd->ElemCount),
                             1,
                             static_cast<uint32_t>(pcmd->IdxOffset + globalIdxOffset),
                             static_cast<int32_t>(pcmd->VtxOffset + globalVtxOffset),
                             0);
        }
        globalIdxOffset += cmdList->IdxBuffer.Size;
        globalVtxOffset += cmdList->VtxBuffer.Size;
    }

    // Restore full scissor
    ghi::setScissor(0, 0, lastDisplayWidth_, lastDisplayHeight_);
}

} // namespace jupiter::rendering::demo




