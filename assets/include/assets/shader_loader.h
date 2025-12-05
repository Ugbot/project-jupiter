#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <atomic>
#include <unordered_map>

/**
 * @file shader_loader.h
 * @brief Hot-reloadable shader loading system
 *
 * Loads SPIR-V shaders from disk, tracks them in AssetDatabase,
 * and monitors for file changes to trigger hot reload.
 *
 * Following CLAUDE.md principles:
 * - Lock-free reload queue
 * - Pre-allocated shader storage
 * - Event-based reload notifications
 */

namespace jupiter {
namespace assets {

// Forward declarations
class AssetDatabase;
class FileWatcher;

/**
 * @brief Shader stage types
 */
enum class ShaderStage : uint32_t {
    VERTEX = 0,
    FRAGMENT = 1,
    COMPUTE = 2,
    GEOMETRY = 3,
    TESS_CONTROL = 4,
    TESS_EVALUATION = 5
};

/**
 * @brief Loaded shader metadata and binary
 */
struct ShaderData {
    std::string uuid;              // Asset UUID
    std::string path;              // Relative path
    ShaderStage stage;             // Shader stage
    std::vector<uint32_t> spirv;   // SPIR-V bytecode
    std::string entryPoint;        // Entry point (usually "main")
    uint32_t version;              // Incremented on reload
    bool isDirty;                  // Needs reload
};

/**
 * @brief Shader reload event
 */
struct ShaderReloadEvent {
    std::string uuid;              // Shader UUID that was reloaded
    uint32_t newVersion;           // New version number
};

/**
 * @brief Callback for shader reload events
 */
using ShaderReloadCallback = std::function<void(const ShaderReloadEvent&)>;

/**
 * @brief Hot-reloadable shader loader
 *
 * Loads SPIR-V shaders, tracks them in AssetDatabase, and monitors for changes.
 * When a shader file changes, it's automatically reloaded and dependents are notified.
 *
 * Thread-Safety: Load/reload operations are thread-safe. Use pollReloads() from main thread.
 */
class ShaderLoader {
public:
    ShaderLoader();
    ~ShaderLoader();

    // No copy or move
    ShaderLoader(const ShaderLoader&) = delete;
    ShaderLoader& operator=(const ShaderLoader&) = delete;
    ShaderLoader(ShaderLoader&&) = delete;
    ShaderLoader& operator=(ShaderLoader&&) = delete;

    /**
     * @brief Initialize shader loader
     * @param database Asset database for metadata storage
     * @param watcher File watcher for hot reload (optional)
     * @return true if successful
     */
    bool init(AssetDatabase* database, FileWatcher* watcher = nullptr);

    /**
     * @brief Shutdown shader loader
     */
    void shutdown();

    /**
     * @brief Load shader from file
     * @param path Path to SPIR-V shader file (.spv)
     * @param stage Shader stage
     * @param entryPoint Entry point name (default: "main")
     * @return Shader UUID, or empty string on failure
     */
    std::string loadShader(const std::string& path, ShaderStage stage, const std::string& entryPoint = "main");

    /**
     * @brief Get loaded shader by UUID
     * @param uuid Shader UUID
     * @param outShader Output shader data
     * @return true if found
     */
    bool getShader(const std::string& uuid, ShaderData& outShader) const;

    /**
     * @brief Get loaded shader by path
     * @param path Shader path
     * @param outShader Output shader data
     * @return true if found
     */
    bool getShaderByPath(const std::string& path, ShaderData& outShader) const;

    /**
     * @brief Manually reload shader from disk
     * @param uuid Shader UUID
     * @return true if successful
     */
    bool reloadShader(const std::string& uuid);

    /**
     * @brief Register callback for shader reloads
     * @param callback Callback function
     */
    void onShaderReload(ShaderReloadCallback callback);

    /**
     * @brief Poll for shader reload events (call from main thread)
     * Processes pending reload events and invokes callbacks
     */
    void pollReloads();

    /**
     * @brief Get number of loaded shaders
     */
    size_t getLoadedShaderCount() const;

    /**
     * @brief Detect shader stage from file extension
     * @param path Shader file path
     * @return Detected shader stage
     */
    static ShaderStage detectStageFromPath(const std::string& path);

    /**
     * @brief Convert shader stage to string
     */
    static const char* stageToString(ShaderStage stage);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    AssetDatabase* database_;
    FileWatcher* watcher_;
    std::atomic<bool> initialized_{false};

    // Loaded shaders (UUID -> ShaderData)
    mutable std::mutex shadersMutex_;
    std::unordered_map<std::string, ShaderData> shaders_;
    std::unordered_map<std::string, std::string> pathToUuid_;

    ShaderReloadCallback reloadCallback_;

    bool loadSpirvFromFile(const std::string& path, std::vector<uint32_t>& outSpirv);
    void onFileChanged(const struct FileChangeEvent& event);
};

} // namespace assets
} // namespace jupiter
