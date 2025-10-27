#include "assets/assets.h"
#include "../../platform/include/platform/platform.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

namespace jupiter {
namespace assets {

// ============================================================================
// Base Asset Implementation
// ============================================================================

class BaseAsset : public Asset {
public:
    BaseAsset(const std::string& name, const std::string& filePath, AssetType type)
        : m_name(name), m_filePath(filePath), m_type(type), m_loaded(false), m_size(0), m_lastModified(0) {}

    virtual ~BaseAsset() = default;

    AssetType getType() const override { return m_type; }
    const std::string& getName() const override { return m_name; }
    const std::string& getFilePath() const override { return m_filePath; }
    bool isLoaded() const override { return m_loaded; }
    size_t getSize() const override { return m_size; }
    uint64_t getLastModified() const override { return m_lastModified; }

protected:
    void setLoaded(bool loaded) { m_loaded = loaded; }
    void setSize(size_t size) { m_size = size; }
    void setLastModified(uint64_t time) { m_lastModified = time; }

private:
    std::string m_name;
    std::string m_filePath;
    AssetType m_type;
    bool m_loaded;
    size_t m_size;
    uint64_t m_lastModified;
};

// ============================================================================
// Texture Asset
// ============================================================================

class TextureAsset : public BaseAsset {
public:
    TextureAsset(const std::string& name, const std::string& filePath)
        : BaseAsset(name, filePath, AssetType::TEXTURE) {}

    bool load() {
        // TODO: Implement actual texture loading
        // For now, just mark as loaded
        setLoaded(true);
        setSize(1024 * 1024); // Dummy size
        setLastModified(platform::Timer::getCurrentTimestamp());
        return true;
    }
};

// ============================================================================
// Mesh Asset
// ============================================================================

class MeshAsset : public BaseAsset {
public:
    MeshAsset(const std::string& name, const std::string& filePath)
        : BaseAsset(name, filePath, AssetType::MESH) {}

    bool load() {
        // TODO: Implement actual mesh loading
        setLoaded(true);
        setSize(2048); // Dummy size
        setLastModified(platform::Timer::getCurrentTimestamp());
        return true;
    }
};

// ============================================================================
// Audio Asset
// ============================================================================

class AudioAsset : public BaseAsset {
public:
    AudioAsset(const std::string& name, const std::string& filePath)
        : BaseAsset(name, filePath, AssetType::AUDIO) {}

    bool load() {
        // TODO: Implement actual audio loading
        setLoaded(true);
        setSize(4096); // Dummy size
        setLastModified(platform::Timer::getCurrentTimestamp());
        return true;
    }
};

// ============================================================================
// Shader Asset
// ============================================================================

class ShaderAsset : public BaseAsset {
public:
    ShaderAsset(const std::string& name, const std::string& filePath)
        : BaseAsset(name, filePath, AssetType::SHADER) {}

    bool load() {
        // TODO: Implement actual shader loading
        setLoaded(true);
        setSize(1024); // Dummy size
        setLastModified(platform::Timer::getCurrentTimestamp());
        return true;
    }
};

// ============================================================================
// Generic Asset
// ============================================================================

class GenericAsset : public BaseAsset {
public:
    GenericAsset(const std::string& name, const std::string& filePath, AssetType type)
        : BaseAsset(name, filePath, type) {}

    bool load() {
        // Generic asset loading - just read file size
        auto fileSize = platform::FileSystem::getFileSize(getFilePath());
        if (fileSize >= 0) {
            setSize(static_cast<size_t>(fileSize));
            setLoaded(true);
            setLastModified(platform::Timer::getCurrentTimestamp());
            return true;
        }
        return false;
    }
};

// ============================================================================
// AssetManager Implementation
// ============================================================================

AssetManager& AssetManager::instance() {
    static AssetManager manager;
    return manager;
}

bool AssetManager::initialize(const AssetConfig& config) {
    if (m_initialized) {
        return true;
    }

    // Set up search paths with priorities
    for (const auto& path : config.searchPaths) {
        addSearchPath(path, 0);
    }

    m_fallbackPath = config.fallbackPath;
    m_maxCacheSize = config.maxCacheSize;

    // Register default asset loaders
    registerLoader(AssetType::TEXTURE, [](const std::string& path) -> std::shared_ptr<Asset> {
        std::filesystem::path fsPath(path);
        std::string name = fsPath.filename().string();
        auto asset = std::make_shared<TextureAsset>(name, path);
        if (asset->load()) {
            return asset;
        }
        return nullptr;
    });

    registerLoader(AssetType::MESH, [](const std::string& path) -> std::shared_ptr<Asset> {
        std::filesystem::path fsPath(path);
        std::string name = fsPath.filename().string();
        auto asset = std::make_shared<MeshAsset>(name, path);
        if (asset->load()) {
            return asset;
        }
        return nullptr;
    });

    registerLoader(AssetType::AUDIO, [](const std::string& path) -> std::shared_ptr<Asset> {
        std::filesystem::path fsPath(path);
        std::string name = fsPath.filename().string();
        auto asset = std::make_shared<AudioAsset>(name, path);
        if (asset->load()) {
            return asset;
        }
        return nullptr;
    });

    registerLoader(AssetType::SHADER, [](const std::string& path) -> std::shared_ptr<Asset> {
        std::filesystem::path fsPath(path);
        std::string name = fsPath.filename().string();
        auto asset = std::make_shared<ShaderAsset>(name, path);
        if (asset->load()) {
            return asset;
        }
        return nullptr;
    });

    // Default loader for unknown types
    registerLoader(AssetType::UNKNOWN, [](const std::string& path) -> std::shared_ptr<Asset> {
        std::filesystem::path fsPath(path);
        std::string name = fsPath.filename().string();

        // Try to determine type from extension
        std::string ext = fsPath.extension().string();
        AssetType type = AssetType::UNKNOWN;

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds") {
            type = AssetType::TEXTURE;
        } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".dae") {
            type = AssetType::MESH;
        } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
            type = AssetType::AUDIO;
        } else if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".geom") {
            type = AssetType::SHADER;
        }

        auto asset = std::make_shared<GenericAsset>(name, path, type);
        if (asset->load()) {
            return asset;
        }
        return nullptr;
    });

    m_initialized = true;
    std::cout << "Asset manager initialized with " << config.searchPaths.size() << " search paths" << std::endl;
    return true;
}

void AssetManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    clearCache();
    m_searchPaths.clear();
    m_loaders.clear();

    m_initialized = false;
    std::cout << "Asset manager shutdown" << std::endl;
}

void AssetManager::addSearchPath(const std::string& path, int priority) {
    // Remove existing path if it exists
    removeSearchPath(path);

    // Insert with priority (lower priority number = higher priority)
    auto it = std::lower_bound(m_searchPaths.begin(), m_searchPaths.end(), priority,
        [](const SearchPath& sp, int prio) { return sp.priority < prio; });

    m_searchPaths.insert(it, {path, priority});
}

void AssetManager::removeSearchPath(const std::string& path) {
    m_searchPaths.erase(
        std::remove_if(m_searchPaths.begin(), m_searchPaths.end(),
            [&path](const SearchPath& sp) { return sp.path == path; }),
        m_searchPaths.end());
}

void AssetManager::setFallbackPath(const std::string& path) {
    m_fallbackPath = path;
}

std::shared_ptr<Asset> AssetManager::loadAsset(const std::string& name, AssetType expectedType) {
    if (!m_initialized) {
        return nullptr;
    }

    // Check cache first
    auto it = m_assetCache.find(name);
    if (it != m_assetCache.end()) {
        return it->second;
    }

    // Find the asset file
    std::string assetPath = resolveAssetPath(name);
    if (assetPath.empty()) {
        std::cout << "Asset not found: " << name << std::endl;
        return nullptr;
    }

    // Load the asset
    auto asset = loadAssetInternal(assetPath, expectedType);
    if (asset) {
        // Add to cache if caching is enabled
        if (!m_assetCache.count(name)) {
            m_assetCache[name] = asset;
            evictCacheIfNeeded();
        }
        std::cout << "Loaded asset: " << name << " (" << asset->getSize() << " bytes)" << std::endl;
    }

    return asset;
}

std::shared_ptr<Asset> AssetManager::loadAssetFromPath(const std::string& path) {
    if (!m_initialized) {
        return nullptr;
    }

    std::filesystem::path fsPath(path);
    std::string name = fsPath.filename().string();

    // Check cache first
    auto it = m_assetCache.find(name);
    if (it != m_assetCache.end()) {
        return it->second;
    }

    // Load directly from path
    auto asset = loadAssetInternal(path, AssetType::UNKNOWN);
    if (asset) {
        m_assetCache[name] = asset;
        evictCacheIfNeeded();
        std::cout << "Loaded asset from path: " << path << std::endl;
    }

    return asset;
}

void AssetManager::unloadAsset(std::shared_ptr<Asset> asset) {
    if (!asset) return;

    // Find and remove from cache
    for (auto it = m_assetCache.begin(); it != m_assetCache.end(); ++it) {
        if (it->second == asset) {
            m_assetCache.erase(it);
            std::cout << "Unloaded asset: " << asset->getName() << std::endl;
            break;
        }
    }
}

void AssetManager::unloadAsset(const std::string& name) {
    auto it = m_assetCache.find(name);
    if (it != m_assetCache.end()) {
        m_assetCache.erase(it);
        std::cout << "Unloaded asset: " << name << std::endl;
    }
}

void AssetManager::clearCache() {
    size_t assetCount = m_assetCache.size();
    m_assetCache.clear();
    std::cout << "Cleared asset cache (" << assetCount << " assets)" << std::endl;
}

AssetManager::CacheStats AssetManager::getCacheStats() const {
    CacheStats stats;
    stats.totalAssets = m_assetCache.size();
    stats.cachedAssets = m_assetCache.size();
    stats.maxCacheSizeBytes = m_maxCacheSize;

    for (const auto& pair : m_assetCache) {
        stats.cacheSizeBytes += pair.second->getSize();
    }

    return stats;
}

std::string AssetManager::findAssetPath(const std::string& name) const {
    return resolveAssetPath(name);
}

bool AssetManager::assetExists(const std::string& name) const {
    return !resolveAssetPath(name).empty();
}

void AssetManager::registerLoader(AssetType type, std::function<std::shared_ptr<Asset>(const std::string&)> loader) {
    m_loaders[type] = loader;
}

void AssetManager::setHotReloadEnabled(bool enabled) {
    m_hotReloadEnabled = enabled;
    // TODO: Implement file watching for hot reload
}

void AssetManager::update() {
    // TODO: Check for file changes if hot reload is enabled
}

std::shared_ptr<Asset> AssetManager::loadAssetInternal(const std::string& path, AssetType expectedType) {
    // Find appropriate loader
    auto loaderIt = m_loaders.find(expectedType);
    if (loaderIt == m_loaders.end()) {
        // Try unknown type loader
        loaderIt = m_loaders.find(AssetType::UNKNOWN);
        if (loaderIt == m_loaders.end()) {
            return nullptr;
        }
    }

    return loaderIt->second(path);
}

void AssetManager::evictCacheIfNeeded() {
    size_t totalSize = 0;
    for (const auto& pair : m_assetCache) {
        totalSize += pair.second->getSize();
    }

    // Simple LRU eviction - remove oldest assets if over limit
    while (totalSize > m_maxCacheSize && !m_assetCache.empty()) {
        // For now, just remove the first asset (TODO: implement proper LRU)
        auto it = m_assetCache.begin();
        totalSize -= it->second->getSize();
        m_assetCache.erase(it);
    }
}

std::string AssetManager::resolveAssetPath(const std::string& name) const {
    // If name is already an absolute path, use it directly
    if (platform::FileSystem::fileExists(name)) {
        return name;
    }

    // Search through configured paths in priority order
    for (const auto& searchPath : m_searchPaths) {
        std::string fullPath = platform::FileSystem::normalizePath(searchPath.path + "/" + name);
        if (platform::FileSystem::fileExists(fullPath)) {
            return fullPath;
        }
    }

    // Try fallback path
    if (!m_fallbackPath.empty()) {
        std::string fallbackPath = platform::FileSystem::normalizePath(m_fallbackPath + "/" + name);
        if (platform::FileSystem::fileExists(fallbackPath)) {
            return fallbackPath;
        }
    }

    return ""; // Not found
}

// ============================================================================
// Legacy API Implementation
// ============================================================================

bool initialize() {
    AssetConfig config;
    // Default search paths
    config.searchPaths = {
        "assets/",           // Local assets directory
        "../assets/",        // Parent assets directory
        "./",                // Current directory
    };

    return AssetManager::instance().initialize(config);
}

bool initialize(const AssetConfig& config) {
    return AssetManager::instance().initialize(config);
}

void shutdown() {
    AssetManager::instance().shutdown();
}

bool loadAsset(const char* filename) {
    if (!filename) return false;

    std::string name(filename);
    auto asset = AssetManager::instance().loadAsset(name);
    return asset != nullptr;
}

AssetManager& getAssetManager() {
    return AssetManager::instance();
}

} // namespace assets
} // namespace jupiter
