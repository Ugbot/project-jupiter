#include "assets/asset_database.h"
#include "assets/mesh_asset.h"
#include "assets/material_asset.h"
#include "assets/image_asset.h"
#include "assets/model_asset.h"
#include "logging/logging.h"
#include "platform/platform.h"

#include <SDL3/SDL_filesystem.h>
#include <sqlite3.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>

namespace jupiter {
namespace assets {

// ============================================================================
// Database Schema
// ============================================================================

static const char* SQL_CREATE_TABLES = R"(
CREATE TABLE IF NOT EXISTS assets (
    uuid TEXT PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    type INTEGER NOT NULL,
    size_bytes INTEGER,
    last_modified INTEGER,
    checksum TEXT,
    format TEXT,
    metadata_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_assets_path ON assets(path);
CREATE INDEX IF NOT EXISTS idx_assets_type ON assets(type);

CREATE TABLE IF NOT EXISTS dependencies (
    asset_uuid TEXT NOT NULL,
    depends_on_uuid TEXT NOT NULL,
    PRIMARY KEY (asset_uuid, depends_on_uuid),
    FOREIGN KEY(asset_uuid) REFERENCES assets(uuid) ON DELETE CASCADE,
    FOREIGN KEY(depends_on_uuid) REFERENCES assets(uuid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_dependencies_asset ON dependencies(asset_uuid);
CREATE INDEX IF NOT EXISTS idx_dependencies_depends_on ON dependencies(depends_on_uuid);

CREATE TABLE IF NOT EXISTS preprocessed_cache (
    source_uuid TEXT PRIMARY KEY,
    preprocessed_path TEXT NOT NULL,
    preprocessed_size INTEGER,
    FOREIGN KEY(source_uuid) REFERENCES assets(uuid) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS path_aliases (
    virtual_path TEXT PRIMARY KEY,
    physical_path TEXT NOT NULL,
    priority INTEGER DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_path_aliases_priority ON path_aliases(priority DESC);

CREATE TABLE IF NOT EXISTS shader_cache (
    shader_uuid TEXT PRIMARY KEY,
    shader_stage INTEGER NOT NULL,
    source_checksum TEXT NOT NULL,
    spirv_blob BLOB NOT NULL,
    spirv_size INTEGER NOT NULL,
    compilation_time INTEGER,
    last_modified INTEGER,
    entry_point TEXT DEFAULT 'main',
    FOREIGN KEY(shader_uuid) REFERENCES assets(uuid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_shader_cache_checksum ON shader_cache(source_checksum);

-- Trigger to notify on shader changes (for hot-reload)
CREATE TRIGGER IF NOT EXISTS trigger_shader_updated
AFTER UPDATE ON shader_cache
FOR EACH ROW
BEGIN
    UPDATE assets SET last_modified = strftime('%s', 'now')
    WHERE uuid = NEW.shader_uuid;
END;

-- Trigger to mark shader as dirty when source asset changes
CREATE TRIGGER IF NOT EXISTS trigger_shader_source_changed
AFTER UPDATE OF checksum ON assets
FOR EACH ROW
WHEN NEW.type = 3  -- AssetType::SHADER
BEGIN
    DELETE FROM shader_cache WHERE shader_uuid = NEW.uuid;
END;
)";

// ============================================================================
// AssetDatabase Implementation
// ============================================================================

AssetDatabase::AssetDatabase() {
}

AssetDatabase::~AssetDatabase() {
    close();
}

bool AssetDatabase::open(const std::string& dbPath) {
    if (db_ != nullptr) {
        LOG_WARN("AssetDatabase", "Database already open, closing first");
        close();
    }

    // Open database
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to open database: %s", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Enable foreign keys
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    // Create tables
    if (!createTables()) {
        LOG_ERROR("AssetDatabase", "Failed to create tables");
        close();
        return false;
    }

    // Prepare statements
    if (!prepareStatements()) {
        LOG_ERROR("AssetDatabase", "Failed to prepare statements");
        close();
        return false;
    }

    LOG_INFO("AssetDatabase", "Opened database: %s", dbPath.c_str());
    return true;
}

void AssetDatabase::close() {
    if (db_ == nullptr) {
        return;
    }

    finalizeStatements();
    sqlite3_close(db_);
    db_ = nullptr;

    LOG_INFO("AssetDatabase", "Closed database");
}

bool AssetDatabase::createTables() {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, SQL_CREATE_TABLES, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to create tables: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool AssetDatabase::prepareStatements() {
    // Insert/update asset
    const char* sqlInsert = R"(
        INSERT OR REPLACE INTO assets
        (uuid, path, type, size_bytes, last_modified, checksum, format, metadata_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (sqlite3_prepare_v2(db_, sqlInsert, -1, &stmtInsertAsset_, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare insert statement");
        return false;
    }

    // Get by path
    const char* sqlGetByPath = "SELECT * FROM assets WHERE path = ?";
    if (sqlite3_prepare_v2(db_, sqlGetByPath, -1, &stmtGetAssetByPath_, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare get-by-path statement");
        return false;
    }

    // Get by UUID
    const char* sqlGetByUuid = "SELECT * FROM assets WHERE uuid = ?";
    if (sqlite3_prepare_v2(db_, sqlGetByUuid, -1, &stmtGetAssetByUuid_, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare get-by-uuid statement");
        return false;
    }

    // Get by type
    const char* sqlGetByType = "SELECT * FROM assets WHERE type = ?";
    if (sqlite3_prepare_v2(db_, sqlGetByType, -1, &stmtGetAssetsByType_, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare get-by-type statement");
        return false;
    }

    return true;
}

void AssetDatabase::finalizeStatements() {
    if (stmtInsertAsset_) sqlite3_finalize(stmtInsertAsset_);
    if (stmtGetAssetByPath_) sqlite3_finalize(stmtGetAssetByPath_);
    if (stmtGetAssetByUuid_) sqlite3_finalize(stmtGetAssetByUuid_);
    if (stmtGetAssetsByType_) sqlite3_finalize(stmtGetAssetsByType_);

    stmtInsertAsset_ = nullptr;
    stmtGetAssetByPath_ = nullptr;
    stmtGetAssetByUuid_ = nullptr;
    stmtGetAssetsByType_ = nullptr;
}

bool AssetDatabase::upsertAsset(const AssetMetadata& metadata) {
    if (!db_ || !stmtInsertAsset_) return false;

    sqlite3_reset(stmtInsertAsset_);

    sqlite3_bind_text(stmtInsertAsset_, 1, metadata.uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertAsset_, 2, metadata.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsertAsset_, 3, static_cast<int>(metadata.type));
    sqlite3_bind_int64(stmtInsertAsset_, 4, metadata.sizeBytes);
    sqlite3_bind_int64(stmtInsertAsset_, 5, metadata.lastModified);
    sqlite3_bind_text(stmtInsertAsset_, 6, metadata.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertAsset_, 7, metadata.format.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertAsset_, 8, metadata.metadataJson.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmtInsertAsset_);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("AssetDatabase", "Failed to insert asset: %s", sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

bool AssetDatabase::getAssetByPath(const std::string& path, AssetMetadata& outMetadata) {
    if (!db_ || !stmtGetAssetByPath_) return false;

    sqlite3_reset(stmtGetAssetByPath_);
    sqlite3_bind_text(stmtGetAssetByPath_, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmtGetAssetByPath_);
    if (rc != SQLITE_ROW) {
        return false;
    }

    outMetadata.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByPath_, 0));
    outMetadata.path = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByPath_, 1));
    outMetadata.type = static_cast<AssetType>(sqlite3_column_int(stmtGetAssetByPath_, 2));
    outMetadata.sizeBytes = sqlite3_column_int64(stmtGetAssetByPath_, 3);
    outMetadata.lastModified = sqlite3_column_int64(stmtGetAssetByPath_, 4);

    const char* checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByPath_, 5));
    if (checksum) outMetadata.checksum = checksum;

    const char* format = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByPath_, 6));
    if (format) outMetadata.format = format;

    const char* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByPath_, 7));
    if (metadata) outMetadata.metadataJson = metadata;

    return true;
}

bool AssetDatabase::getAssetByUuid(const std::string& uuid, AssetMetadata& outMetadata) {
    if (!db_ || !stmtGetAssetByUuid_) return false;

    sqlite3_reset(stmtGetAssetByUuid_);
    sqlite3_bind_text(stmtGetAssetByUuid_, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmtGetAssetByUuid_);
    if (rc != SQLITE_ROW) {
        return false;
    }

    outMetadata.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByUuid_, 0));
    outMetadata.path = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByUuid_, 1));
    outMetadata.type = static_cast<AssetType>(sqlite3_column_int(stmtGetAssetByUuid_, 2));
    outMetadata.sizeBytes = sqlite3_column_int64(stmtGetAssetByUuid_, 3);
    outMetadata.lastModified = sqlite3_column_int64(stmtGetAssetByUuid_, 4);

    const char* checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByUuid_, 5));
    if (checksum) outMetadata.checksum = checksum;

    const char* format = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByUuid_, 6));
    if (format) outMetadata.format = format;

    const char* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetByUuid_, 7));
    if (metadata) outMetadata.metadataJson = metadata;

    return true;
}

size_t AssetDatabase::getAssetsByType(AssetType type, std::vector<AssetMetadata>& outAssets) {
    if (!db_ || !stmtGetAssetsByType_) return 0;

    sqlite3_reset(stmtGetAssetsByType_);
    sqlite3_bind_int(stmtGetAssetsByType_, 1, static_cast<int>(type));

    size_t count = 0;
    while (sqlite3_step(stmtGetAssetsByType_) == SQLITE_ROW) {
        AssetMetadata metadata;
        metadata.uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetsByType_, 0));
        metadata.path = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetsByType_, 1));
        metadata.type = static_cast<AssetType>(sqlite3_column_int(stmtGetAssetsByType_, 2));
        metadata.sizeBytes = sqlite3_column_int64(stmtGetAssetsByType_, 3);
        metadata.lastModified = sqlite3_column_int64(stmtGetAssetsByType_, 4);

        const char* checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetsByType_, 5));
        if (checksum) metadata.checksum = checksum;

        const char* format = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetsByType_, 6));
        if (format) metadata.format = format;

        const char* metaJson = reinterpret_cast<const char*>(sqlite3_column_text(stmtGetAssetsByType_, 7));
        if (metaJson) metadata.metadataJson = metaJson;

        outAssets.push_back(std::move(metadata));
        count++;
    }

    return count;
}

bool AssetDatabase::deleteAsset(const std::string& uuid) {
    if (!db_) return false;

    const char* sql = "DELETE FROM assets WHERE uuid = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool AssetDatabase::addDependency(const std::string& assetUuid, const std::string& dependsOnUuid) {
    if (!db_) return false;

    const char* sql = "INSERT OR IGNORE INTO dependencies (asset_uuid, depends_on_uuid) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, assetUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dependsOnUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

size_t AssetDatabase::getDependencies(const std::string& assetUuid, std::vector<std::string>& outDependencies) {
    if (!db_) return 0;

    const char* sql = "SELECT depends_on_uuid FROM dependencies WHERE asset_uuid = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, assetUuid.c_str(), -1, SQLITE_TRANSIENT);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outDependencies.push_back(uuid);
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

size_t AssetDatabase::getDependents(const std::string& assetUuid, std::vector<std::string>& outDependents) {
    if (!db_) return 0;

    const char* sql = "SELECT asset_uuid FROM dependencies WHERE depends_on_uuid = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, assetUuid.c_str(), -1, SQLITE_TRANSIENT);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* uuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outDependents.push_back(uuid);
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

bool AssetDatabase::setPreprocessedCache(const PreprocessedCacheEntry& entry) {
    if (!db_) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO preprocessed_cache
        (source_uuid, preprocessed_path, preprocessed_size)
        VALUES (?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, entry.sourceUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.preprocessedPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, entry.preprocessedSize);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool AssetDatabase::getPreprocessedCache(const std::string& sourceUuid, PreprocessedCacheEntry& outEntry) {
    if (!db_) return false;

    const char* sql = "SELECT * FROM preprocessed_cache WHERE source_uuid = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, sourceUuid.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    outEntry.sourceUuid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    outEntry.preprocessedPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    outEntry.preprocessedSize = sqlite3_column_int64(stmt, 2);

    sqlite3_finalize(stmt);
    return true;
}

size_t AssetDatabase::scanDirectory(const std::string& rootPath) {
    // TODO: Implement recursive directory scanning
    // For now, return 0
    LOG_WARN("AssetDatabase", "scanDirectory not yet implemented");
    return 0;
}

size_t AssetDatabase::getAssetCount() {
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM assets";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

std::string AssetDatabase::generateUuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    // Generate UUID v4
    uint64_t part1 = dist(gen);
    uint64_t part2 = dist(gen);

    // Set version (4) and variant bits
    part1 = (part1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << ((part1 >> 32) & 0xFFFFFFFF) << "-";
    ss << std::setw(4) << ((part1 >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << (part1 & 0xFFFF) << "-";
    ss << std::setw(4) << ((part2 >> 48) & 0xFFFF) << "-";
    ss << std::setw(12) << (part2 & 0xFFFFFFFFFFFFULL);

    return ss.str();
}

std::string AssetDatabase::computeChecksum(const std::string& filePath) {
    // Simple checksum for now (file size + modification time)
    // TODO: Implement proper SHA256
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(filePath.c_str(), &info)) {
        return "";
    }

    std::stringstream ss;
    ss << std::hex << (info.size ^ static_cast<uint64_t>(info.modify_time));
    return ss.str();
}

AssetType AssetDatabase::detectAssetType(const std::string& path) {
    // Extract file extension
    const char* lastDot = strrchr(path.c_str(), '.');
    if (!lastDot) {
        return AssetType::UNKNOWN;
    }
    std::string ext = lastDot;

    // Convert to lowercase
    for (char& c : ext) {
        c = std::tolower(c);
    }

    // Textures
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" ||
        ext == ".bmp" || ext == ".psd" || ext == ".dds" || ext == ".hdr") {
        return AssetType::TEXTURE;
    }

    // Meshes
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
        ext == ".dae" || ext == ".3ds" || ext == ".blend" || ext == ".stl") {
        return AssetType::MESH;
    }

    // Shaders
    if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".spv" ||
        ext == ".glsl" || ext == ".hlsl" || ext == ".spirv") {
        return AssetType::SHADER;
    }

    // Audio
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
        return AssetType::AUDIO;
    }

    // Scripts
    if (ext == ".lua" || ext == ".py" || ext == ".js") {
        return AssetType::SCRIPT;
    }

    // Fonts
    if (ext == ".ttf" || ext == ".otf") {
        return AssetType::FONT;
    }

    // Config
    if (ext == ".json" || ext == ".xml" || ext == ".yaml" || ext == ".yml" || ext == ".toml") {
        return AssetType::CONFIG;
    }

    return AssetType::UNKNOWN;
}

std::string AssetDatabase::getFileFormat(const std::string& path) {
    const char* lastDot = strrchr(path.c_str(), '.');
    if (!lastDot) {
        return "";
    }
    std::string ext = lastDot;
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);  // Remove leading dot
    }
    return ext;
}

// ============================================================================
// Path Aliasing Implementation
// ============================================================================

bool AssetDatabase::addPathAlias(const std::string& virtualPath,
                                  const std::string& physicalPath,
                                  int priority) {
    if (!isOpen()) {
        LOG_ERROR("AssetDatabase", "Cannot add path alias: database not open");
        return false;
    }

    const char* sql = "INSERT OR REPLACE INTO path_aliases (virtual_path, physical_path, priority) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare add path alias statement");
        return false;
    }

    sqlite3_bind_text(stmt, 1, virtualPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, physicalPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, priority);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("AssetDatabase", "Failed to add path alias: %s", sqlite3_errmsg(db_));
        return false;
    }

    LOG_INFO("AssetDatabase", "Added path alias: '%s' -> '%s' (priority %d)",
             virtualPath.c_str(), physicalPath.c_str(), priority);
    return true;
}

bool AssetDatabase::removePathAlias(const std::string& virtualPath) {
    if (!isOpen()) {
        LOG_ERROR("AssetDatabase", "Cannot remove path alias: database not open");
        return false;
    }

    const char* sql = "DELETE FROM path_aliases WHERE virtual_path = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare remove path alias statement");
        return false;
    }

    sqlite3_bind_text(stmt, 1, virtualPath.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("AssetDatabase", "Failed to remove path alias: %s", sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

std::string AssetDatabase::resolveAssetPath(const std::string& path) {
    if (!isOpen()) {
        return path;  // No database, return as-is
    }

    // Get all aliases sorted by priority (descending)
    const char* sql = "SELECT virtual_path, physical_path FROM path_aliases ORDER BY priority DESC";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_WARN("AssetDatabase", "Failed to prepare resolve path statement");
        return path;
    }

    // Check each alias to see if path starts with virtual_path
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* virtualPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* physicalPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        if (!virtualPath || !physicalPath) continue;

        size_t vlen = strlen(virtualPath);
        // Check if path starts with virtualPath
        if (path.compare(0, vlen, virtualPath) == 0) {
            // Replace virtual prefix with physical prefix
            std::string resolved = std::string(physicalPath) + path.substr(vlen);
            sqlite3_finalize(stmt);
            return resolved;
        }
    }

    sqlite3_finalize(stmt);
    return path;  // No alias matched, return original
}

size_t AssetDatabase::getPathAliases(std::vector<PathAlias>& outAliases) {
    outAliases.clear();

    if (!isOpen()) {
        return 0;
    }

    const char* sql = "SELECT virtual_path, physical_path, priority FROM path_aliases ORDER BY priority DESC";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare get path aliases statement");
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PathAlias alias;
        alias.virtualPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        alias.physicalPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        alias.priority = sqlite3_column_int(stmt, 2);
        outAliases.push_back(alias);
    }

    sqlite3_finalize(stmt);
    return outAliases.size();
}

// ============================================================================
// Shader Cache Implementation
// ============================================================================

bool AssetDatabase::cacheShader(const std::string& shaderUuid, uint32_t stage,
                                const std::string& sourceChecksum,
                                const void* spirvData, size_t spirvSize,
                                const std::string& entryPoint) {
    if (!isOpen()) {
        return false;
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO shader_cache
        (shader_uuid, shader_stage, source_checksum, spirv_blob, spirv_size,
         compilation_time, last_modified, entry_point)
        VALUES (?, ?, ?, ?, ?, strftime('%s', 'now'), strftime('%s', 'now'), ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare cache shader statement");
        return false;
    }

    sqlite3_bind_text(stmt, 1, shaderUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(stage));
    sqlite3_bind_text(stmt, 3, sourceChecksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 4, spirvData, static_cast<int>(spirvSize), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(spirvSize));
    sqlite3_bind_text(stmt, 6, entryPoint.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (success) {
        LOG_INFO("AssetDatabase", "Cached shader %s (SPIR-V size: %zu bytes)",
                shaderUuid.c_str(), spirvSize);
    }

    return success;
}

bool AssetDatabase::getCachedShader(const std::string& shaderUuid,
                                   const std::string& sourceChecksum,
                                   uint8_t*& outSpirvData, size_t& outSpirvSize) {
    if (!isOpen()) {
        return false;
    }

    const char* sql = R"(
        SELECT spirv_blob, spirv_size
        FROM shader_cache
        WHERE shader_uuid = ? AND source_checksum = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare get cached shader statement");
        return false;
    }

    sqlite3_bind_text(stmt, 1, shaderUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sourceChecksum.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blobData = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_int(stmt, 1);

        if (blobData && blobSize > 0) {
            outSpirvData = new uint8_t[blobSize];
            std::memcpy(outSpirvData, blobData, blobSize);
            outSpirvSize = static_cast<size_t>(blobSize);
            found = true;
            LOG_INFO("AssetDatabase", "Retrieved cached shader %s (%zu bytes)",
                    shaderUuid.c_str(), outSpirvSize);
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

bool AssetDatabase::isShaderCacheValid(const std::string& shaderUuid,
                                       const std::string& sourceChecksum) {
    if (!isOpen()) {
        return false;
    }

    const char* sql = R"(
        SELECT COUNT(*) FROM shader_cache
        WHERE shader_uuid = ? AND source_checksum = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, shaderUuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sourceChecksum.c_str(), -1, SQLITE_TRANSIENT);

    bool valid = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        valid = sqlite3_column_int(stmt, 0) > 0;
    }

    sqlite3_finalize(stmt);
    return valid;
}

bool AssetDatabase::invalidateShaderCache(const std::string& shaderUuid) {
    if (!isOpen()) {
        return false;
    }

    const char* sql = "DELETE FROM shader_cache WHERE shader_uuid = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("AssetDatabase", "Failed to prepare invalidate shader cache statement");
        return false;
    }

    sqlite3_bind_text(stmt, 1, shaderUuid.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (success) {
        LOG_INFO("AssetDatabase", "Invalidated shader cache for %s", shaderUuid.c_str());
    }

    return success;
}

// ============================================================================
// Asset Structure Storage (In-Memory)
// ============================================================================

std::string AssetDatabase::storeMeshAsset(const MeshAsset& meshAsset) {
    if (meshAsset.uuid.empty()) {
        LOG_ERROR("AssetDatabase", "Cannot store mesh asset with empty UUID");
        return "";
    }

    meshAssets_[meshAsset.uuid] = meshAsset;
    LOG_INFO("AssetDatabase", "Stored mesh asset: %s", meshAsset.uuid.c_str());
    return meshAsset.uuid;
}

bool AssetDatabase::getMeshAsset(const std::string& uuid, MeshAsset& outMeshAsset) {
    auto it = meshAssets_.find(uuid);
    if (it == meshAssets_.end()) {
        return false;
    }

    outMeshAsset = it->second;
    return true;
}

std::string AssetDatabase::storeMaterialAsset(const MaterialAsset& materialAsset) {
    if (materialAsset.uuid.empty()) {
        LOG_ERROR("AssetDatabase", "Cannot store material asset with empty UUID");
        return "";
    }

    materialAssets_[materialAsset.uuid] = materialAsset;
    LOG_INFO("AssetDatabase", "Stored material asset: %s", materialAsset.uuid.c_str());
    return materialAsset.uuid;
}

bool AssetDatabase::getMaterialAsset(const std::string& uuid, MaterialAsset& outMaterialAsset) {
    auto it = materialAssets_.find(uuid);
    if (it == materialAssets_.end()) {
        return false;
    }

    outMaterialAsset = it->second;
    return true;
}

std::string AssetDatabase::storeImageAsset(const ImageAsset& imageAsset) {
    if (imageAsset.uuid.empty()) {
        LOG_ERROR("AssetDatabase", "Cannot store image asset with empty UUID");
        return "";
    }

    imageAssets_[imageAsset.uuid] = imageAsset;
    LOG_INFO("AssetDatabase", "Stored image asset: %s (%dx%d)",
            imageAsset.uuid.c_str(), imageAsset.width, imageAsset.height);
    return imageAsset.uuid;
}

bool AssetDatabase::getImageAsset(const std::string& uuid, ImageAsset& outImageAsset) {
    auto it = imageAssets_.find(uuid);
    if (it == imageAssets_.end()) {
        return false;
    }

    outImageAsset = it->second;
    return true;
}

std::string AssetDatabase::storeModelAsset(const ModelAsset& modelAsset) {
    if (modelAsset.uuid.empty()) {
        LOG_ERROR("AssetDatabase", "Cannot store model asset with empty UUID");
        return "";
    }

    modelAssets_[modelAsset.uuid] = modelAsset;
    LOG_INFO("AssetDatabase", "Stored model asset: %s (%zu meshes, %zu materials)",
            modelAsset.uuid.c_str(), modelAsset.meshes.size(), modelAsset.materials.size());
    return modelAsset.uuid;
}

bool AssetDatabase::getModelAsset(const std::string& uuid, ModelAsset& outModelAsset) {
    auto it = modelAssets_.find(uuid);
    if (it == modelAssets_.end()) {
        return false;
    }

    outModelAsset = it->second;
    return true;
}

} // namespace assets
} // namespace jupiter
