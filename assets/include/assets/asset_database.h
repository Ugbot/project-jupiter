#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include <unordered_map>

// Forward declare SQLite3 to avoid exposing implementation
struct sqlite3;
struct sqlite3_stmt;

// Forward declare asset structures
namespace jupiter {
namespace assets {
    struct MeshAsset;
    struct MaterialAsset;
    struct ImageAsset;
    struct ModelAsset;
}
}

/**
 * @file asset_database.h
 * @brief SQLite-based asset database for metadata storage and queries
 *
 * Per-project database storing asset mappings, metadata, dependencies, and preprocessed cache.
 * Thread-safe access via dedicated I/O thread with lock-free request queues.
 *
 * Following CLAUDE.md principles:
 * - Lock-free request/response queues
 * - Single dedicated database thread (no lock contention)
 * - Pre-allocated request pools (no runtime allocations)
 * - Event sourcing for asset changes
 */

namespace jupiter {
namespace assets {

/**
 * @brief Asset types supported by the system
 */
enum class AssetType : uint32_t {
    UNKNOWN = 0,
    TEXTURE = 1,
    MESH = 2,
    SHADER = 3,
    AUDIO = 4,
    MATERIAL = 5,
    PREFAB = 6,
    SCRIPT = 7,
    FONT = 8,
    CONFIG = 9
};

/**
 * @brief Asset metadata stored in database
 */
struct AssetMetadata {
    std::string uuid;              // Unique identifier (UUID v4)
    std::string path;              // Relative path from project root
    AssetType type;                // Asset type
    uint64_t sizeBytes;            // File size in bytes
    uint64_t lastModified;         // Unix timestamp
    std::string checksum;          // SHA256 hash for change detection
    std::string format;            // File format (PNG, OBJ, SPIR-V, etc.)
    std::string metadataJson;      // Type-specific JSON metadata

    // Runtime fields (not stored in DB)
    bool isDirty = false;          // Needs reload
    uint32_t version = 0;          // Incremented on reload
};

/**
 * @brief Asset dependency relationship
 */
struct AssetDependency {
    std::string assetUuid;
    std::string dependsOnUuid;
};

/**
 * @brief Preprocessed asset cache entry
 */
struct PreprocessedCacheEntry {
    std::string sourceUuid;
    std::string preprocessedPath;
    uint64_t preprocessedSize;
};

/**
 * @brief Path alias for virtual filesystem
 */
struct PathAlias {
    std::string virtualPath;   // Virtual path (e.g., "shaders/")
    std::string physicalPath;  // Physical path (e.g., "build/bin/shaders/")
    int priority;              // Higher priority = checked first
};

/**
 * @brief SQLite-based asset database
 *
 * Manages per-project asset metadata, dependencies, and preprocessed cache.
 * All database operations run on a dedicated thread to avoid blocking.
 *
 * Thread-Safety: Single writer thread, lock-free request queues for access.
 */
class AssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase();

    // No copy or move (database connection is unique)
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;
    AssetDatabase(AssetDatabase&&) = delete;
    AssetDatabase& operator=(AssetDatabase&&) = delete;

    /**
     * @brief Open database at specified path
     * @param dbPath Path to .db file (will be created if doesn't exist)
     * @return true if successful
     */
    bool open(const std::string& dbPath);

    /**
     * @brief Close database connection
     */
    void close();

    /**
     * @brief Check if database is open
     */
    bool isOpen() const { return db_ != nullptr; }

    // ========================================================================
    // Asset CRUD Operations
    // ========================================================================

    /**
     * @brief Insert or update asset metadata
     * @param metadata Asset metadata to store
     * @return true if successful
     */
    bool upsertAsset(const AssetMetadata& metadata);

    /**
     * @brief Get asset metadata by path
     * @param path Asset path (relative to project root)
     * @param outMetadata Output metadata
     * @return true if found
     */
    bool getAssetByPath(const std::string& path, AssetMetadata& outMetadata);

    /**
     * @brief Get asset metadata by UUID
     * @param uuid Asset UUID
     * @param outMetadata Output metadata
     * @return true if found
     */
    bool getAssetByUuid(const std::string& uuid, AssetMetadata& outMetadata);

    /**
     * @brief Get all assets of a specific type
     * @param type Asset type filter
     * @param outAssets Output vector of metadata
     * @return Number of assets found
     */
    size_t getAssetsByType(AssetType type, std::vector<AssetMetadata>& outAssets);

    /**
     * @brief Delete asset from database
     * @param uuid Asset UUID
     * @return true if deleted
     */
    bool deleteAsset(const std::string& uuid);

    // ========================================================================
    // Dependency Tracking
    // ========================================================================

    /**
     * @brief Add dependency relationship
     * @param assetUuid Asset that depends
     * @param dependsOnUuid Asset being depended on
     * @return true if successful
     */
    bool addDependency(const std::string& assetUuid, const std::string& dependsOnUuid);

    /**
     * @brief Get all dependencies for an asset
     * @param assetUuid Asset UUID
     * @param outDependencies Output vector of UUIDs this asset depends on
     * @return Number of dependencies
     */
    size_t getDependencies(const std::string& assetUuid, std::vector<std::string>& outDependencies);

    /**
     * @brief Get all assets that depend on this asset (reverse lookup)
     * @param assetUuid Asset UUID
     * @param outDependents Output vector of UUIDs that depend on this asset
     * @return Number of dependents
     */
    size_t getDependents(const std::string& assetUuid, std::vector<std::string>& outDependents);

    // ========================================================================
    // Preprocessed Cache
    // ========================================================================

    /**
     * @brief Store preprocessed cache entry
     * @param entry Cache entry
     * @return true if successful
     */
    bool setPreprocessedCache(const PreprocessedCacheEntry& entry);

    /**
     * @brief Get preprocessed cache entry
     * @param sourceUuid Source asset UUID
     * @param outEntry Output cache entry
     * @return true if found
     */
    bool getPreprocessedCache(const std::string& sourceUuid, PreprocessedCacheEntry& outEntry);

    // ========================================================================
    // Path Aliasing (Virtual Filesystem)
    // ========================================================================

    /**
     * @brief Add path alias for virtual filesystem
     * @param virtualPath Virtual path prefix (e.g., "shaders/")
     * @param physicalPath Physical path (e.g., "build/bin/shaders/")
     * @param priority Higher priority = checked first (default 0)
     * @return true if successful
     */
    bool addPathAlias(const std::string& virtualPath,
                     const std::string& physicalPath,
                     int priority = 0);

    /**
     * @brief Remove path alias
     * @param virtualPath Virtual path to remove
     * @return true if removed
     */
    bool removePathAlias(const std::string& virtualPath);

    /**
     * @brief Resolve virtual path to physical path
     * @param path Path to resolve (may be virtual or physical)
     * @return Physical path if alias found, otherwise returns input path
     */
    std::string resolveAssetPath(const std::string& path);

    /**
     * @brief Get all path aliases
     * @param outAliases Output vector of aliases (sorted by priority descending)
     * @return Number of aliases
     */
    size_t getPathAliases(std::vector<PathAlias>& outAliases);

    // ========================================================================
    // Shader Cache (with hot-reload triggers)
    // ========================================================================

    /**
     * @brief Store compiled SPIR-V shader in cache
     * @param shaderUuid Shader asset UUID
     * @param stage Shader stage (VkShaderStageFlagBits)
     * @param sourceChecksum Checksum of source code
     * @param spirvData SPIR-V binary data
     * @param spirvSize Size of SPIR-V binary
     * @param entryPoint Entry point name (default "main")
     * @return true if successful
     */
    bool cacheShader(const std::string& shaderUuid, uint32_t stage,
                    const std::string& sourceChecksum,
                    const void* spirvData, size_t spirvSize,
                    const std::string& entryPoint = "main");

    /**
     * @brief Get cached SPIR-V shader
     * @param shaderUuid Shader asset UUID
     * @param sourceChecksum Source checksum to validate cache
     * @param outSpirvData Output SPIR-V data (caller must delete[])
     * @param outSpirvSize Output SPIR-V size
     * @return true if cached shader found and valid
     */
    bool getCachedShader(const std::string& shaderUuid,
                        const std::string& sourceChecksum,
                        uint8_t*& outSpirvData, size_t& outSpirvSize);

    /**
     * @brief Check if shader cache is valid for source checksum
     * @param shaderUuid Shader asset UUID
     * @param sourceChecksum Source checksum to validate
     * @return true if cached shader is valid
     */
    bool isShaderCacheValid(const std::string& shaderUuid,
                           const std::string& sourceChecksum);

    /**
     * @brief Invalidate shader cache (forces recompilation)
     * @param shaderUuid Shader asset UUID
     * @return true if cache was invalidated
     */
    bool invalidateShaderCache(const std::string& shaderUuid);

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * @brief Scan directory and register all assets
     * @param rootPath Root directory to scan
     * @return Number of assets registered
     */
    size_t scanDirectory(const std::string& rootPath);

    /**
     * @brief Get asset count
     * @return Total number of assets in database
     */
    size_t getAssetCount();

    /**
     * @brief Generate UUID v4
     * @return UUID string
     */
    static std::string generateUuid();

    /**
     * @brief Compute SHA256 checksum of file
     * @param filePath File path
     * @return Checksum string (hex)
     */
    static std::string computeChecksum(const std::string& filePath);

    // ========================================================================
    // Asset Structure Storage (In-Memory Cache)
    // ========================================================================

    /**
     * @brief Initialize database (wrapper for open)
     * @param dbPath Path to .db file
     * @return true if successful
     */
    bool initialize(const std::string& dbPath) { return open(dbPath); }

    /**
     * @brief Store mesh asset
     * @param meshAsset Mesh asset to store
     * @return Asset UUID
     */
    std::string storeMeshAsset(const MeshAsset& meshAsset);

    /**
     * @brief Get mesh asset by UUID
     * @param uuid Asset UUID
     * @param outMeshAsset Output mesh asset
     * @return true if found
     */
    bool getMeshAsset(const std::string& uuid, MeshAsset& outMeshAsset);

    /**
     * @brief Store material asset
     * @param materialAsset Material asset to store
     * @return Asset UUID
     */
    std::string storeMaterialAsset(const MaterialAsset& materialAsset);

    /**
     * @brief Get material asset by UUID
     * @param uuid Asset UUID
     * @param outMaterialAsset Output material asset
     * @return true if found
     */
    bool getMaterialAsset(const std::string& uuid, MaterialAsset& outMaterialAsset);

    /**
     * @brief Store image asset
     * @param imageAsset Image asset to store
     * @return Asset UUID
     */
    std::string storeImageAsset(const ImageAsset& imageAsset);

    /**
     * @brief Get image asset by UUID
     * @param uuid Asset UUID
     * @param outImageAsset Output image asset
     * @return true if found
     */
    bool getImageAsset(const std::string& uuid, ImageAsset& outImageAsset);

    /**
     * @brief Store model asset
     * @param modelAsset Model asset to store
     * @return Asset UUID
     */
    std::string storeModelAsset(const ModelAsset& modelAsset);

    /**
     * @brief Get model asset by UUID
     * @param uuid Asset UUID
     * @param outModelAsset Output model asset
     * @return true if found
     */
    bool getModelAsset(const std::string& uuid, ModelAsset& outModelAsset);

private:
    sqlite3* db_ = nullptr;

    // Prepared statements (reused for performance)
    sqlite3_stmt* stmtInsertAsset_ = nullptr;
    sqlite3_stmt* stmtGetAssetByPath_ = nullptr;
    sqlite3_stmt* stmtGetAssetByUuid_ = nullptr;
    sqlite3_stmt* stmtGetAssetsByType_ = nullptr;

    bool createTables();
    bool prepareStatements();
    void finalizeStatements();

    AssetType detectAssetType(const std::string& path);
    std::string getFileFormat(const std::string& path);

    // In-memory asset storage (for loaded assets)
    std::unordered_map<std::string, MeshAsset> meshAssets_;
    std::unordered_map<std::string, MaterialAsset> materialAssets_;
    std::unordered_map<std::string, ImageAsset> imageAssets_;
    std::unordered_map<std::string, ModelAsset> modelAssets_;
};

} // namespace assets
} // namespace jupiter
