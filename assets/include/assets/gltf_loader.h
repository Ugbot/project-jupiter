#pragma once

#include "asset_database.h"
#include "model_asset.h"
#include <string>

namespace jupiter {
namespace assets {

/**
 * @brief GLTF 2.0 model loader (CPU-only, no GPU resources)
 *
 * Parses GLTF files and converts them to asset data structures.
 * No Vulkan types - can be used without GPU context.
 *
 * Following CLAUDE.md:
 * - No runtime allocations during parsing (pre-allocate vectors)
 * - Platform-agnostic (no GPU dependencies)
 * - Lock-free (single-threaded loading)
 *
 * Benefits:
 * - Server-side asset loading
 * - Background thread loading
 * - Asset baking pipelines
 * - Asset validation/processing tools
 *
 * Usage:
 * @code
 * AssetDatabase assetDB;
 * GLTFLoader loader(assetDB);
 * std::string modelUuid = loader.loadModel("models/helmet.gltf");
 *
 * ModelAsset model;
 * if (assetDB.getModelAsset(modelUuid, model)) {
 *     // Use model data...
 * }
 * @endcode
 */
class GLTFLoader {
public:
    /**
     * @brief Constructor
     * @param database Asset database for storing loaded assets
     */
    explicit GLTFLoader(AssetDatabase& database);

    ~GLTFLoader() = default;

    // Non-copyable
    GLTFLoader(const GLTFLoader&) = delete;
    GLTFLoader& operator=(const GLTFLoader&) = delete;

    /**
     * @brief Load GLTF model from file (CPU-only, no GPU)
     *
     * Parses GLTF file and stores all assets in the database:
     * - ModelAsset (with scene graph)
     * - MeshAssets (vertex/index data)
     * - MaterialAssets (PBR properties)
     * - ImageAssets (decoded pixel data)
     *
     * All assets are assigned UUIDs and stored in the database.
     * The model references other assets by UUID (not pointers).
     *
     * @param filepath Path to .gltf or .glb file
     * @return Model UUID on success, empty string on failure
     */
    std::string loadModel(const std::string& filepath);

private:
    AssetDatabase& database_;

    // Loading helpers (all CPU-only, no GPU types)
    bool loadImages(void* gltfModel, const std::string& basePath,
                   ModelAsset& outModel);

    bool loadMaterials(void* gltfModel, const std::vector<std::string>& imageUuids,
                      ModelAsset& outModel);

    bool loadMeshes(void* gltfModel, ModelAsset& outModel);

    bool loadNodes(void* gltfModel, ModelAsset& outModel);

    // Skeletal animation loading
    bool loadSkins(void* gltfModel, ModelAsset& outModel);

    bool loadAnimations(void* gltfModel, ModelAsset& outModel);

    // Helper: Extract vertex data from GLTF accessor
    bool extractVertexAttribute(void* gltfModel, int accessorIndex,
                               std::vector<float>& outData, size_t expectedCount);

    // Helper: Extract index data from GLTF accessor
    bool extractIndexData(void* gltfModel, int accessorIndex,
                         std::vector<uint32_t>& outIndices);

    // Helper: Generate tangents using MikkTSpace-like algorithm
    void generateTangents(const std::vector<float>& positions,
                         const std::vector<float>& normals,
                         const std::vector<float>& texcoords,
                         const std::vector<uint32_t>& indices,
                         std::vector<float>& outTangents,
                         uint32_t vertexCount);
};

} // namespace assets
} // namespace jupiter
