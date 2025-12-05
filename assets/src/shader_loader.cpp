#include "assets/shader_loader.h"
#include "assets/asset_database.h"
#include "assets/file_watcher.h"
#include "memory/queues.h"
#include "logging/logging.h"

#include <SDL3/SDL_filesystem.h>
#include <fstream>
#include <cstring>

namespace jupiter {
namespace assets {

// Lock-free queue for shader reload events
using ReloadQueue = memory::MPMCQueue<ShaderReloadEvent, 64>;

struct ShaderLoader::Impl {
    ReloadQueue reloadQueue;
};

ShaderLoader::ShaderLoader()
    : impl_(std::make_unique<Impl>())
    , database_(nullptr)
    , watcher_(nullptr) {
}

ShaderLoader::~ShaderLoader() {
    shutdown();
}

bool ShaderLoader::init(AssetDatabase* database, FileWatcher* watcher) {
    if (!database) {
        LOG_ERROR("ShaderLoader", "AssetDatabase is required");
        return false;
    }

    database_ = database;
    watcher_ = watcher;
    initialized_.store(true, std::memory_order_release);

    LOG_INFO("ShaderLoader", "Initialized (hot-reload: %s)", watcher ? "enabled" : "disabled");
    return true;
}

void ShaderLoader::shutdown() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return;
    }

    initialized_.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(shadersMutex_);
    shaders_.clear();
    pathToUuid_.clear();

    LOG_INFO("ShaderLoader", "Shutdown complete");
}

std::string ShaderLoader::loadShader(const std::string& path, ShaderStage stage, const std::string& entryPoint) {
    if (!initialized_.load(std::memory_order_acquire)) {
        LOG_ERROR("ShaderLoader", "Not initialized");
        return "";
    }

    // Check if already loaded
    {
        std::lock_guard<std::mutex> lock(shadersMutex_);
        auto it = pathToUuid_.find(path);
        if (it != pathToUuid_.end()) {
            LOG_WARN("ShaderLoader", "Shader already loaded: %s (UUID: %s)", path.c_str(), it->second.c_str());
            return it->second;
        }
    }

    // Load SPIR-V from file
    std::vector<uint32_t> spirv;
    if (!loadSpirvFromFile(path, spirv)) {
        LOG_ERROR("ShaderLoader", "Failed to load SPIR-V: %s", path.c_str());
        return "";
    }

    // Generate UUID for this shader
    std::string uuid = AssetDatabase::generateUuid();

    // Register in database
    AssetMetadata metadata;
    metadata.uuid = uuid;
    metadata.path = path;
    metadata.type = AssetType::SHADER;
    metadata.sizeBytes = spirv.size() * sizeof(uint32_t);

    SDL_PathInfo pathInfo;
    if (SDL_GetPathInfo(path.c_str(), &pathInfo)) {
        metadata.lastModified = static_cast<uint64_t>(pathInfo.modify_time);
    }
    metadata.checksum = AssetDatabase::computeChecksum(path);
    metadata.format = "SPIR-V";

    // Store stage in metadata JSON
    metadata.metadataJson = std::string("{\"stage\":\"") + stageToString(stage) + "\",\"entryPoint\":\"" + entryPoint + "\"}";

    if (!database_->upsertAsset(metadata)) {
        LOG_ERROR("ShaderLoader", "Failed to register shader in database: %s", path.c_str());
        return "";
    }

    // Store in loaded shaders
    ShaderData shaderData;
    shaderData.uuid = uuid;
    shaderData.path = path;
    shaderData.stage = stage;
    shaderData.spirv = std::move(spirv);
    shaderData.entryPoint = entryPoint;
    shaderData.version = 1;
    shaderData.isDirty = false;

    {
        std::lock_guard<std::mutex> lock(shadersMutex_);
        shaders_[uuid] = std::move(shaderData);
        pathToUuid_[path] = uuid;
    }

    LOG_INFO("ShaderLoader", "Loaded shader: %s (UUID: %s, stage: %s, size: %zu bytes)",
             path.c_str(), uuid.c_str(), stageToString(stage), metadata.sizeBytes);

    return uuid;
}

bool ShaderLoader::getShader(const std::string& uuid, ShaderData& outShader) const {
    std::lock_guard<std::mutex> lock(shadersMutex_);
    auto it = shaders_.find(uuid);
    if (it == shaders_.end()) {
        return false;
    }
    outShader = it->second;
    return true;
}

bool ShaderLoader::getShaderByPath(const std::string& path, ShaderData& outShader) const {
    std::lock_guard<std::mutex> lock(shadersMutex_);
    auto it = pathToUuid_.find(path);
    if (it == pathToUuid_.end()) {
        return false;
    }
    auto shaderIt = shaders_.find(it->second);
    if (shaderIt == shaders_.end()) {
        return false;
    }
    outShader = shaderIt->second;
    return true;
}

bool ShaderLoader::reloadShader(const std::string& uuid) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    // Get current shader data
    ShaderData currentShader;
    {
        std::lock_guard<std::mutex> lock(shadersMutex_);
        auto it = shaders_.find(uuid);
        if (it == shaders_.end()) {
            LOG_ERROR("ShaderLoader", "Shader not found for reload: %s", uuid.c_str());
            return false;
        }
        currentShader = it->second;
    }

    // Reload SPIR-V from file
    std::vector<uint32_t> newSpirv;
    if (!loadSpirvFromFile(currentShader.path, newSpirv)) {
        LOG_ERROR("ShaderLoader", "Failed to reload SPIR-V: %s", currentShader.path.c_str());
        return false;
    }

    // Update shader data
    uint32_t newVersion = currentShader.version + 1;
    {
        std::lock_guard<std::mutex> lock(shadersMutex_);
        auto it = shaders_.find(uuid);
        if (it != shaders_.end()) {
            it->second.spirv = std::move(newSpirv);
            it->second.version = newVersion;
            it->second.isDirty = false;
        }
    }

    // Update database metadata
    AssetMetadata metadata;
    if (database_->getAssetByUuid(uuid, metadata)) {
        metadata.sizeBytes = newSpirv.size() * sizeof(uint32_t);

        SDL_PathInfo pathInfo;
        if (SDL_GetPathInfo(currentShader.path.c_str(), &pathInfo)) {
            metadata.lastModified = static_cast<uint64_t>(pathInfo.modify_time);
        }
        metadata.checksum = AssetDatabase::computeChecksum(currentShader.path);
        database_->upsertAsset(metadata);
    }

    // Queue reload event
    ShaderReloadEvent event;
    event.uuid = uuid;
    event.newVersion = newVersion;

    if (!impl_->reloadQueue.push(event)) {
        LOG_WARN("ShaderLoader", "Reload queue full, callback may be delayed");
    }

    LOG_INFO("ShaderLoader", "Reloaded shader: %s (version: %u)", currentShader.path.c_str(), newVersion);
    return true;
}

void ShaderLoader::onShaderReload(ShaderReloadCallback callback) {
    reloadCallback_ = callback;
}

void ShaderLoader::pollReloads() {
    if (!reloadCallback_) {
        return;
    }

    ShaderReloadEvent event;
    while (impl_->reloadQueue.pop(event)) {
        reloadCallback_(event);
    }
}

size_t ShaderLoader::getLoadedShaderCount() const {
    std::lock_guard<std::mutex> lock(shadersMutex_);
    return shaders_.size();
}

bool ShaderLoader::loadSpirvFromFile(const std::string& path, std::vector<uint32_t>& outSpirv) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_ERROR("ShaderLoader", "Failed to open file: %s", path.c_str());
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % sizeof(uint32_t) != 0) {
        LOG_ERROR("ShaderLoader", "Invalid SPIR-V file size (not multiple of 4): %s", path.c_str());
        return false;
    }

    outSpirv.resize(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(outSpirv.data()), fileSize);

    if (!file) {
        LOG_ERROR("ShaderLoader", "Failed to read file: %s", path.c_str());
        return false;
    }

    // Validate SPIR-V magic number
    if (outSpirv.empty() || outSpirv[0] != 0x07230203) {
        LOG_ERROR("ShaderLoader", "Invalid SPIR-V magic number: %s", path.c_str());
        return false;
    }

    return true;
}

void ShaderLoader::onFileChanged(const FileChangeEvent& event) {
    if (event.type != FileChangeType::MODIFIED) {
        return; // Only care about modifications for shader reload
    }

    // Check if this file is a loaded shader
    std::string uuid;
    {
        std::lock_guard<std::mutex> lock(shadersMutex_);
        auto it = pathToUuid_.find(event.path);
        if (it == pathToUuid_.end()) {
            return; // Not a tracked shader
        }
        uuid = it->second;
    }

    LOG_INFO("ShaderLoader", "Detected shader change: %s", event.path.c_str());
    reloadShader(uuid);
}

ShaderStage ShaderLoader::detectStageFromPath(const std::string& path) {
    // Extract filename and extension
    const char* lastSlash = strrchr(path.c_str(), '/');
    if (!lastSlash) {
        lastSlash = strrchr(path.c_str(), '\\');
    }
    const char* filename = lastSlash ? (lastSlash + 1) : path.c_str();

    const char* lastDot = strrchr(filename, '.');
    std::string ext = lastDot ? lastDot : "";
    std::string stem = lastDot ? std::string(filename, lastDot - filename) : filename;

    // Check for common patterns
    if (stem.find("vert") != std::string::npos || ext == ".vert") {
        return ShaderStage::VERTEX;
    }
    if (stem.find("frag") != std::string::npos || ext == ".frag") {
        return ShaderStage::FRAGMENT;
    }
    if (stem.find("comp") != std::string::npos || ext == ".comp") {
        return ShaderStage::COMPUTE;
    }
    if (stem.find("geom") != std::string::npos || ext == ".geom") {
        return ShaderStage::GEOMETRY;
    }
    if (stem.find("tesc") != std::string::npos || ext == ".tesc") {
        return ShaderStage::TESS_CONTROL;
    }
    if (stem.find("tese") != std::string::npos || ext == ".tese") {
        return ShaderStage::TESS_EVALUATION;
    }

    LOG_WARN("ShaderLoader", "Could not detect shader stage from path: %s, defaulting to VERTEX", path.c_str());
    return ShaderStage::VERTEX;
}

const char* ShaderLoader::stageToString(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX: return "VERTEX";
        case ShaderStage::FRAGMENT: return "FRAGMENT";
        case ShaderStage::COMPUTE: return "COMPUTE";
        case ShaderStage::GEOMETRY: return "GEOMETRY";
        case ShaderStage::TESS_CONTROL: return "TESS_CONTROL";
        case ShaderStage::TESS_EVALUATION: return "TESS_EVALUATION";
        default: return "UNKNOWN";
    }
}

} // namespace assets
} // namespace jupiter
