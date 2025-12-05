#pragma once

#include <string>
#include <cstdint>
#include <functional>

namespace jupiter {
namespace assets {

// Forward declarations
struct MeshAsset;
struct MaterialAsset;
struct ImageAsset;
struct ModelAsset;

/**
 * @brief Type-safe asset handle
 *
 * Wraps a UUID string with type information to prevent mixing asset types.
 * Following CLAUDE.md: POD type with no overhead.
 *
 * Benefits:
 * - Compile-time type safety (can't pass MeshHandle where MaterialHandle expected)
 * - Zero runtime overhead (just a string wrapper)
 * - Hot-reload support via version tracking
 * - Serialization-friendly (UUID is a string)
 *
 * Example:
 * @code
 * MeshHandle meshHandle = assetDB.loadMesh("cube.obj");
 * MaterialHandle matHandle = assetDB.loadMaterial("metal.mat");
 *
 * // This would be a compile error:
 * // assetDB.getMesh(matHandle);  // Type mismatch!
 * @endcode
 */
template<typename T>
struct AssetHandle {
    std::string uuid;      // Unique asset identifier
    uint32_t version;      // Version for hot-reload tracking (0 = initial)

    /**
     * @brief Default constructor (invalid handle)
     */
    AssetHandle() : version(0) {}

    /**
     * @brief Constructor with UUID
     */
    explicit AssetHandle(const std::string& uuid_) : uuid(uuid_), version(0) {}

    /**
     * @brief Constructor with UUID and version
     */
    AssetHandle(const std::string& uuid_, uint32_t version_)
        : uuid(uuid_), version(version_) {}

    /**
     * @brief Check if handle is valid
     */
    bool isValid() const {
        return !uuid.empty();
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const AssetHandle<T>& other) const {
        return uuid == other.uuid;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const AssetHandle<T>& other) const {
        return uuid != other.uuid;
    }

    /**
     * @brief Less-than comparison (for std::map/std::set)
     */
    bool operator<(const AssetHandle<T>& other) const {
        return uuid < other.uuid;
    }
};

// Type aliases for specific asset types
using MeshHandle = AssetHandle<MeshAsset>;
using MaterialHandle = AssetHandle<MaterialAsset>;
using ImageHandle = AssetHandle<ImageAsset>;
using ModelHandle = AssetHandle<ModelAsset>;

} // namespace assets
} // namespace jupiter

// Hash function for AssetHandle (for std::unordered_map)
namespace std {
    template<typename T>
    struct hash<jupiter::assets::AssetHandle<T>> {
        size_t operator()(const jupiter::assets::AssetHandle<T>& handle) const {
            return std::hash<std::string>{}(handle.uuid);
        }
    };
}
