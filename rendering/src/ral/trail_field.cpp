/**
 * @file trail_field.cpp
 * @brief GHI-based trail field implementation
 */

#include "rendering/ral/trail_field.h"
#include "logging/logging.h"
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace rendering {
namespace ral {

TrailField::~TrailField() {
    shutdown();
}

void TrailField::shutdown() {
    if (!initialized_) return;

    LOG_INFO("TrailField", "Shutting down trail field");

    // Destroy textures
    if (intensityA_.isValid()) ghi::destroyTexture(intensityA_);
    if (intensityB_.isValid()) ghi::destroyTexture(intensityB_);
    if (dirA_.isValid()) ghi::destroyTexture(dirA_);
    if (dirB_.isValid()) ghi::destroyTexture(dirB_);

    // Destroy buffer
    if (eventsBuffer_.isValid()) ghi::destroyBuffer(eventsBuffer_);

    // Destroy shader
    if (updateShader_.isValid()) ghi::destroyShader(updateShader_);

    initialized_ = false;
}

bool TrailField::initialize(const TrailFieldConfig& config) {
    config_ = config;

    LOG_INFO("TrailField", "Initializing GHI trail field (%.0fm, %ux%u)",
             config_.worldSize, config_.resolution, config_.resolution);

    // Check compute shader support
    if (!ghi::hasComputeShaders()) {
        LOG_ERROR("TrailField", "Compute shaders not supported - trail field requires compute");
        return false;
    }

    // Preallocate CPU staging vector
    eventsStaging_.reserve(MAX_TRAIL_EVENTS);

    // Create ping-pong textures
    if (!createTextures()) {
        LOG_ERROR("TrailField", "Failed to create trail textures");
        return false;
    }

    // Create events storage buffer
    if (!createBuffers()) {
        LOG_ERROR("TrailField", "Failed to create trail buffers");
        return false;
    }

    // Create compute shader
    if (!createShader()) {
        LOG_ERROR("TrailField", "Failed to create trail update shader");
        return false;
    }

    // Set initial ping-pong pointers
    currentIntensity_ = intensityA_;
    currentDir_ = dirA_;
    prevIntensity_ = intensityB_;
    prevDir_ = dirB_;

    initialized_ = true;
    LOG_INFO("TrailField", "Trail field initialized successfully");
    return true;
}

bool TrailField::createTextures() {
    // Create intensity textures (R16F for trail strength)
    ghi::TextureCreateInfo intensityInfo;
    intensityInfo.type = ghi::TextureType::Texture2D;
    intensityInfo.format = ghi::Format::R16_FLOAT;
    intensityInfo.width = config_.resolution;
    intensityInfo.height = config_.resolution;
    intensityInfo.mipLevels = 1;
    intensityInfo.usage = ghi::TextureUsage::Sampled | ghi::TextureUsage::Storage;
    intensityInfo.minFilter = ghi::Filter::Linear;
    intensityInfo.magFilter = ghi::Filter::Linear;
    intensityInfo.wrapS = ghi::WrapMode::ClampToEdge;
    intensityInfo.wrapT = ghi::WrapMode::ClampToEdge;

    intensityA_ = ghi::createTexture(intensityInfo);
    if (!intensityA_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create intensity A texture");
        return false;
    }

    intensityB_ = ghi::createTexture(intensityInfo);
    if (!intensityB_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create intensity B texture");
        return false;
    }

    // Create direction textures (RG16F for 2D bend direction)
    ghi::TextureCreateInfo dirInfo = intensityInfo;
    dirInfo.format = ghi::Format::RG16_FLOAT;

    dirA_ = ghi::createTexture(dirInfo);
    if (!dirA_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create direction A texture");
        return false;
    }

    dirB_ = ghi::createTexture(dirInfo);
    if (!dirB_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create direction B texture");
        return false;
    }

    LOG_INFO("TrailField", "Created ping-pong trail textures (%ux%u)",
             config_.resolution, config_.resolution);
    return true;
}

bool TrailField::createBuffers() {
    // Create events storage buffer
    ghi::BufferCreateInfo eventsInfo;
    eventsInfo.type = ghi::BufferType::Storage;
    eventsInfo.usage = ghi::BufferUsage::Dynamic;
    eventsInfo.size = sizeof(TrailEvent) * MAX_TRAIL_EVENTS;

    eventsBuffer_ = ghi::createBuffer(eventsInfo);
    if (!eventsBuffer_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create events buffer");
        return false;
    }

    LOG_INFO("TrailField", "Created events storage buffer (%zu bytes)", eventsInfo.size);
    return true;
}

bool TrailField::createShader() {
    // Load trail update compute shader
    ghi::ShaderSource shaderSource;
    shaderSource.computePath = "rendering/shaders/compute/trail_update.comp.spv";

    updateShader_ = ghi::createComputeShader(shaderSource);
    if (!updateShader_.isValid()) {
        LOG_ERROR("TrailField", "Failed to create trail update compute shader");
        return false;
    }

    LOG_INFO("TrailField", "Created trail update compute shader");
    return true;
}

void TrailField::pushEvent(const TrailEvent& event) {
    if (eventsStaging_.size() < MAX_TRAIL_EVENTS) {
        eventsStaging_.push_back(event);
    }
}

void TrailField::clearEvents() {
    eventsStaging_.clear();
}

void TrailField::update(float dtSeconds, const glm::vec2& newOrigin) {
    if (!initialized_) return;

    // Upload events to GPU
    if (!eventsStaging_.empty()) {
        size_t uploadSize = sizeof(TrailEvent) * eventsStaging_.size();
        ghi::updateBuffer(eventsBuffer_, 0, uploadSize, eventsStaging_.data());
    }

    // Bind compute shader
    ghi::bindComputeShader(updateShader_);

    // Bind resources
    // Set 0: Previous textures (read)
    ghi::bindTexture(prevIntensity_, 0, 0);
    ghi::bindTexture(prevDir_, 0, 1);

    // Set 0: Output textures (write) - storage images
    ghi::bindStorageTexture(currentIntensity_, 0, 2);
    ghi::bindStorageTexture(currentDir_, 0, 3);

    // Set 0: Events buffer
    ghi::bindStorageBuffer(eventsBuffer_, 0, 4);

    // Dispatch (8x8 workgroups)
    uint32_t groupsX = (config_.resolution + 7) / 8;
    uint32_t groupsY = (config_.resolution + 7) / 8;
    ghi::dispatch(groupsX, groupsY, 1);

    // Memory barrier to ensure writes complete before reads
    ghi::memoryBarrier();

    // Swap ping-pong
    pingPong_ = 1 - pingPong_;

    if (pingPong_ == 0) {
        currentIntensity_ = intensityA_;
        currentDir_ = dirA_;
        prevIntensity_ = intensityB_;
        prevDir_ = dirB_;
    } else {
        currentIntensity_ = intensityB_;
        currentDir_ = dirB_;
        prevIntensity_ = intensityA_;
        prevDir_ = dirA_;
    }

    // Update origin for next frame
    prevOrigin_ = currentOrigin_;
    currentOrigin_ = newOrigin;

    // Clear events for next frame
    eventsStaging_.clear();
}

void TrailField::setRelaxSeconds(float seconds) {
    config_.relaxSeconds = std::clamp(seconds, 5.0f, 20.0f);
}

} // namespace ral
} // namespace rendering
} // namespace jupiter
