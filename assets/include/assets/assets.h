#pragma once

#ifdef _WIN32
    #ifdef ASSETS_EXPORTS
        #define ASSETS_API __declspec(dllexport)
    #elif defined(ASSETS_IMPORTS)
        #define ASSETS_API __declspec(dllimport)
    #else
        #define ASSETS_API
    #endif
#else
    #define ASSETS_API
#endif

#include <string>
#include <vector>
#include <memory>
#include <functional>

// Include asset database types first
#include "assets/asset_database.h"

namespace jupiter {
namespace assets {

// Forward declarations
class AssetManager;

/**
 * @brief Asset loading configuration
 */
struct AssetConfig {
    std::vector<std::string> searchPaths;  // Directories to search for assets
    bool enableCaching = true;            // Cache loaded assets in memory
    bool enableHotReload = false;         // Watch files for changes (development)
    size_t maxCacheSize = 100 * 1024 * 1024; // Max cache size in bytes (100MB)
    std::string fallbackPath;             // Fallback directory for missing assets
};

/**
 * @brief Generic asset handle
 */
class ASSETS_API Asset {
public:
    virtual ~Asset() = default;

    /**
     * @brief Get asset type
     */
    virtual AssetType getType() const = 0;

    /**
     * @brief Get asset name/identifier
     */
    virtual const std::string& getName() const = 0;

    /**
     * @brief Get asset file path
     */
    virtual const std::string& getFilePath() const = 0;

    /**
     * @brief Check if asset is loaded
     */
    virtual bool isLoaded() const = 0;

    /**
     * @brief Get asset size in bytes
     */
    virtual size_t getSize() const = 0;

    /**
     * @brief Get last modification time
     */
    virtual uint64_t getLastModified() const = 0;
};

/**
 * @brief Asset manager - central hub for all asset operations
 */
class ASSETS_API AssetManager {
public:
    /**
     * @brief Get singleton instance
     */
    static AssetManager& instance();

    /**
     * @brief Initialize with configuration
     * @param config Asset loading configuration
     * @return true if initialization successful
     */
    bool initialize(const AssetConfig& config);

    /**
     * @brief Shutdown asset manager
     */
    void shutdown();

    /**
     * @brief Add a search path for assets
     * @param path Directory path to search
     * @param priority Search priority (lower = higher priority)
     */
    void addSearchPath(const std::string& path, int priority = 0);

    /**
     * @brief Remove a search path
     * @param path Directory path to remove
     */
    void removeSearchPath(const std::string& path);

    /**
     * @brief Set fallback search path for missing assets
     * @param path Fallback directory
     */
    void setFallbackPath(const std::string& path);

    /**
     * @brief Load an asset by name
     * @param name Asset name (without path)
     * @param type Expected asset type (for validation)
     * @return Asset pointer, or nullptr if loading failed
     */
    std::shared_ptr<Asset> loadAsset(const std::string& name, AssetType type = AssetType::UNKNOWN);

    /**
     * @brief Load an asset from specific path
     * @param path Full path to asset file
     * @return Asset pointer, or nullptr if loading failed
     */
    std::shared_ptr<Asset> loadAssetFromPath(const std::string& path);

    /**
     * @brief Unload an asset
     * @param asset Asset to unload
     */
    void unloadAsset(std::shared_ptr<Asset> asset);

    /**
     * @brief Unload asset by name
     * @param name Asset name
     */
    void unloadAsset(const std::string& name);

    /**
     * @brief Clear asset cache
     */
    void clearCache();

    /**
     * @brief Get cache statistics
     */
    struct CacheStats {
        size_t totalAssets = 0;
        size_t cachedAssets = 0;
        size_t cacheSizeBytes = 0;
        size_t maxCacheSizeBytes = 0;
    };
    CacheStats getCacheStats() const;

    /**
     * @brief Find asset file path
     * @param name Asset name
     * @return Full path if found, empty string otherwise
     */
    std::string findAssetPath(const std::string& name) const;

    /**
     * @brief Check if asset exists
     * @param name Asset name
     * @return true if asset can be found
     */
    bool assetExists(const std::string& name) const;

    /**
     * @brief Register asset loader for custom types
     * @param type Asset type to handle
     * @param loader Loader function
     */
    void registerLoader(AssetType type, std::function<std::shared_ptr<Asset>(const std::string&)> loader);

    /**
     * @brief Enable/disable hot reload
     * @param enabled true to enable file watching
     */
    void setHotReloadEnabled(bool enabled);

    /**
     * @brief Update asset manager (call regularly for hot reload)
     */
    void update();

private:
    AssetManager() = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    struct SearchPath {
        std::string path;
        int priority;
    };

    std::vector<SearchPath> m_searchPaths;
    std::string m_fallbackPath;
    std::unordered_map<std::string, std::shared_ptr<Asset>> m_assetCache;
    std::unordered_map<AssetType, std::function<std::shared_ptr<Asset>(const std::string&)>> m_loaders;

    bool m_initialized = false;
    bool m_hotReloadEnabled = false;
    size_t m_maxCacheSize = 100 * 1024 * 1024; // 100MB

    std::shared_ptr<Asset> loadAssetInternal(const std::string& path, AssetType expectedType);
    void evictCacheIfNeeded();
    std::string resolveAssetPath(const std::string& name) const;
};

/**
 * @brief Initialize the assets subsystem with default config
 * @return true if initialization was successful, false otherwise
 */
ASSETS_API bool initialize();

/**
 * @brief Initialize the assets subsystem with custom config
 * @param config Asset configuration
 * @return true if initialization was successful, false otherwise
 */
ASSETS_API bool initialize(const AssetConfig& config);

/**
 * @brief Shutdown the assets subsystem
 */
ASSETS_API void shutdown();

/**
 * @brief Load an asset (legacy API for backward compatibility)
 * @param filename Asset filename
 * @return true if asset was loaded successfully, false otherwise
 */
ASSETS_API bool loadAsset(const char* filename);

/**
 * @brief Get the asset manager instance
 * @return Reference to asset manager
 */
ASSETS_API AssetManager& getAssetManager();

} // namespace assets
} // namespace jupiter

// Include additional asset system components
#include "assets/file_watcher.h"
#include "assets/shader_loader.h"
