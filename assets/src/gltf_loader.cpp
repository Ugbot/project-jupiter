#include "assets/gltf_loader.h"
#include "assets/mesh_asset.h"
#include "assets/material_asset.h"
#include "assets/image_asset.h"
#include "logging/logging.h"

// TinyGLTF for GLTF 2.0 parsing
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <cstring>
#include <cfloat>
#include <cmath>
#include <set>
#include <unordered_map>

namespace jupiter {
namespace assets {

GLTFLoader::GLTFLoader(AssetDatabase& database)
    : database_(database) {
}

std::string GLTFLoader::loadModel(const std::string& filepath) {
    LOG_INFO("GLTFLoader", "Loading GLTF model: %s", filepath.c_str());

    // Parse GLTF file using TinyGLTF
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = false;
    if (filepath.find(".glb") != std::string::npos) {
        success = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filepath);
    } else {
        success = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filepath);
    }

    if (!warn.empty()) {
        LOG_WARN("GLTFLoader", "GLTF Warning: %s", warn.c_str());
    }

    if (!err.empty()) {
        LOG_ERROR("GLTFLoader", "GLTF Error: %s", err.c_str());
        return "";
    }

    if (!success) {
        LOG_ERROR("GLTFLoader", "Failed to load GLTF file: %s", filepath.c_str());
        return "";
    }

    LOG_INFO("GLTFLoader", "Parsed GLTF: %zu scenes, %zu nodes, %zu meshes, %zu materials, %zu images",
             gltfModel.scenes.size(), gltfModel.nodes.size(), gltfModel.meshes.size(),
             gltfModel.materials.size(), gltfModel.images.size());

    // Extract base path for texture loading
    std::string basePath = filepath.substr(0, filepath.find_last_of("/\\") + 1);

    // Create model asset
    ModelAsset modelAsset;
    modelAsset.uuid = AssetDatabase::generateUuid();
    modelAsset.name = gltfModel.scenes.empty() ? "Unnamed" : gltfModel.scenes[0].name;
    modelAsset.sourceFilePath = filepath;

    // Reserve space for assets (avoid reallocation)
    modelAsset.imageUuids.reserve(gltfModel.images.size());
    modelAsset.materials.reserve(gltfModel.materials.size());
    modelAsset.meshes.reserve(gltfModel.meshes.size());
    modelAsset.nodes.reserve(gltfModel.nodes.size());

    // Load assets in order: images → materials → meshes → scene graph
    if (!loadImages(&gltfModel, basePath, modelAsset)) {
        LOG_ERROR("GLTFLoader", "Failed to load images");
        return "";
    }

    if (!loadMaterials(&gltfModel, modelAsset.imageUuids, modelAsset)) {
        LOG_ERROR("GLTFLoader", "Failed to load materials");
        return "";
    }

    if (!loadMeshes(&gltfModel, modelAsset)) {
        LOG_ERROR("GLTFLoader", "Failed to load meshes");
        return "";
    }

    if (!loadNodes(&gltfModel, modelAsset)) {
        LOG_ERROR("GLTFLoader", "Failed to load scene graph");
        return "";
    }

    // Load skeletal animation data (optional - models without skins still work)
    if (!gltfModel.skins.empty()) {
        if (!loadSkins(&gltfModel, modelAsset)) {
            LOG_WARN("GLTFLoader", "Failed to load skins, continuing without animation");
        }
    }

    if (!gltfModel.animations.empty()) {
        if (!loadAnimations(&gltfModel, modelAsset)) {
            LOG_WARN("GLTFLoader", "Failed to load animations, continuing without animation");
        }
    }

    // Store model in database and get UUID
    std::string modelUuid = database_.storeModelAsset(modelAsset);

    if (modelUuid.empty()) {
        LOG_ERROR("GLTFLoader", "Failed to store model in database");
        return "";
    }

    LOG_INFO("GLTFLoader", "Successfully loaded GLTF model: %s", modelUuid.c_str());
    LOG_INFO("GLTFLoader", "  - %zu meshes, %zu materials, %zu images, %zu nodes",
             modelAsset.meshes.size(), modelAsset.materials.size(),
             modelAsset.imageUuids.size(), modelAsset.nodes.size());
    if (!modelAsset.skins.empty() || !modelAsset.animationUuids.empty()) {
        LOG_INFO("GLTFLoader", "  - %zu skins, %zu animations (skeletal animation enabled)",
                 modelAsset.skins.size(), modelAsset.animationUuids.size());
    }

    return modelUuid;
}

bool GLTFLoader::loadImages(void* gltfModelPtr, const std::string& basePath,
                            ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    for (size_t i = 0; i < gltfModel->images.size(); i++) {
        const tinygltf::Image& gltfImage = gltfModel->images[i];

        ImageAsset imageAsset;
        imageAsset.uuid = AssetDatabase::generateUuid();
        imageAsset.name = gltfImage.name.empty() ? ("Image_" + std::to_string(i)) : gltfImage.name;
        imageAsset.sourceFilePath = basePath + gltfImage.uri;

        // Determine format
        switch (gltfImage.component) {
            case 1: imageAsset.format = ImageFormat::R8; break;
            case 3: imageAsset.format = ImageFormat::RGB8; break;
            case 4: imageAsset.format = ImageFormat::RGBA8; break;
            default:
                LOG_ERROR("GLTFLoader", "Unsupported image component count: %d", gltfImage.component);
                return false;
        }

        // Format defaults to LINEAR (will be updated to sRGB for baseColor/emissive later)
        // All PBR textures except albedo/emissive should be LINEAR
        // (normal maps, metallic-roughness, occlusion must stay LINEAR for correct calculations)

        imageAsset.width = gltfImage.width;
        imageAsset.height = gltfImage.height;
        imageAsset.bytesPerPixel = gltfImage.component;

        // Copy pixel data
        imageAsset.pixelData.resize(gltfImage.image.size());
        std::memcpy(imageAsset.pixelData.data(), gltfImage.image.data(), gltfImage.image.size());

        // Store image in database
        std::string imageUuid = database_.storeImageAsset(imageAsset);
        if (imageUuid.empty()) {
            LOG_ERROR("GLTFLoader", "Failed to store image %zu in database", i);
            return false;
        }

        outModel.imageUuids.push_back(imageUuid);
        LOG_INFO("GLTFLoader", "Loaded image %zu: %s from '%s' (%dx%d, %zu bytes)",
                 i, imageAsset.name.c_str(), gltfImage.uri.c_str(),
                 imageAsset.width, imageAsset.height, imageAsset.pixelData.size());
    }

    return true;
}

bool GLTFLoader::loadMaterials(void* gltfModelPtr, const std::vector<std::string>& imageUuids,
                               ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    for (size_t i = 0; i < gltfModel->materials.size(); i++) {
        const tinygltf::Material& gltfMat = gltfModel->materials[i];
        MaterialAsset material;
        material.uuid = AssetDatabase::generateUuid();

        material.name = gltfMat.name.empty() ? ("Material_" + std::to_string(i)) : gltfMat.name;

        // PBR Metallic-Roughness factors
        if (gltfMat.values.find("baseColorFactor") != gltfMat.values.end()) {
            const auto& factor = gltfMat.values.at("baseColorFactor").ColorFactor();
            material.baseColorFactor[0] = static_cast<float>(factor[0]);
            material.baseColorFactor[1] = static_cast<float>(factor[1]);
            material.baseColorFactor[2] = static_cast<float>(factor[2]);
            material.baseColorFactor[3] = static_cast<float>(factor[3]);
        }

        if (gltfMat.values.find("metallicFactor") != gltfMat.values.end()) {
            material.metallicFactor = static_cast<float>(gltfMat.values.at("metallicFactor").Factor());
        }

        if (gltfMat.values.find("roughnessFactor") != gltfMat.values.end()) {
            material.roughnessFactor = static_cast<float>(gltfMat.values.at("roughnessFactor").Factor());
        }

        // Texture indices (GLTF texture index → image UUID)
        auto getImageUuid = [&](int texIndex) -> std::string {
            if (texIndex >= 0 && texIndex < static_cast<int>(gltfModel->textures.size())) {
                int imageIndex = gltfModel->textures[texIndex].source;
                if (imageIndex >= 0 && imageIndex < static_cast<int>(imageUuids.size())) {
                    return imageUuids[imageIndex];
                }
            }
            return "";
        };

        if (gltfMat.values.find("baseColorTexture") != gltfMat.values.end()) {
            int texIndex = gltfMat.values.at("baseColorTexture").TextureIndex();
            material.baseColorTextureUuid = getImageUuid(texIndex);
            LOG_INFO("GLTFLoader", "  baseColorTexture: texIndex=%d -> imageIndex=%d",
                     texIndex, (texIndex >= 0 && texIndex < (int)gltfModel->textures.size()) ?
                     gltfModel->textures[texIndex].source : -1);
        }

        if (gltfMat.values.find("metallicRoughnessTexture") != gltfMat.values.end()) {
            int texIndex = gltfMat.values.at("metallicRoughnessTexture").TextureIndex();
            material.metallicRoughnessTextureUuid = getImageUuid(texIndex);
            LOG_INFO("GLTFLoader", "  metallicRoughnessTexture: texIndex=%d -> imageIndex=%d",
                     texIndex, (texIndex >= 0 && texIndex < (int)gltfModel->textures.size()) ?
                     gltfModel->textures[texIndex].source : -1);
        }

        // Additional textures
        if (gltfMat.additionalValues.find("normalTexture") != gltfMat.additionalValues.end()) {
            int texIndex = gltfMat.additionalValues.at("normalTexture").TextureIndex();
            material.normalTextureUuid = getImageUuid(texIndex);
            LOG_INFO("GLTFLoader", "  normalTexture: texIndex=%d -> imageIndex=%d",
                     texIndex, (texIndex >= 0 && texIndex < (int)gltfModel->textures.size()) ?
                     gltfModel->textures[texIndex].source : -1);
        }

        if (gltfMat.additionalValues.find("occlusionTexture") != gltfMat.additionalValues.end()) {
            int texIndex = gltfMat.additionalValues.at("occlusionTexture").TextureIndex();
            material.occlusionTextureUuid = getImageUuid(texIndex);
            LOG_INFO("GLTFLoader", "  occlusionTexture: texIndex=%d -> imageIndex=%d",
                     texIndex, (texIndex >= 0 && texIndex < (int)gltfModel->textures.size()) ?
                     gltfModel->textures[texIndex].source : -1);
        }

        if (gltfMat.additionalValues.find("emissiveTexture") != gltfMat.additionalValues.end()) {
            int texIndex = gltfMat.additionalValues.at("emissiveTexture").TextureIndex();
            material.emissiveTextureUuid = getImageUuid(texIndex);
            LOG_INFO("GLTFLoader", "  emissiveTexture: texIndex=%d -> imageIndex=%d",
                     texIndex, (texIndex >= 0 && texIndex < (int)gltfModel->textures.size()) ?
                     gltfModel->textures[texIndex].source : -1);
        }

        if (gltfMat.additionalValues.find("emissiveFactor") != gltfMat.additionalValues.end()) {
            const auto& factor = gltfMat.additionalValues.at("emissiveFactor").ColorFactor();
            material.emissiveFactor[0] = static_cast<float>(factor[0]);
            material.emissiveFactor[1] = static_cast<float>(factor[1]);
            material.emissiveFactor[2] = static_cast<float>(factor[2]);
        }

        // Alpha mode
        if (gltfMat.alphaMode == "OPAQUE") {
            material.alphaMode = MaterialAsset::AlphaMode::OPAQUE;
        } else if (gltfMat.alphaMode == "MASK") {
            material.alphaMode = MaterialAsset::AlphaMode::MASK;
        } else if (gltfMat.alphaMode == "BLEND") {
            material.alphaMode = MaterialAsset::AlphaMode::BLEND;
        }

        material.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
        material.doubleSided = gltfMat.doubleSided;

        // Store material UUID for reference
        std::string matUuid = database_.storeMaterialAsset(material);
        material.uuid = matUuid;

        outModel.materials.push_back(material);

        LOG_INFO("GLTFLoader", "Loaded material %zu: %s (albedo: %s, metalRough: %s, normal: %s)",
                 i, material.name.c_str(),
                 !material.baseColorTextureUuid.empty() ? "yes" : "no",
                 !material.metallicRoughnessTextureUuid.empty() ? "yes" : "no",
                 !material.normalTextureUuid.empty() ? "yes" : "no");
    }

    // Post-process: Mark baseColor and emissive textures as sRGB
    // (All other PBR textures must remain LINEAR for correct lighting calculations)
    std::set<std::string> srgbImageUuids;
    for (const auto& material : outModel.materials) {
        if (!material.baseColorTextureUuid.empty()) {
            srgbImageUuids.insert(material.baseColorTextureUuid);
        }
        if (!material.emissiveTextureUuid.empty()) {
            srgbImageUuids.insert(material.emissiveTextureUuid);
        }
    }

    // Update image formats in database
    LOG_INFO("GLTFLoader", "Updating %zu images to sRGB format (albedo/emissive)", srgbImageUuids.size());
    for (const std::string& uuid : srgbImageUuids) {
        ImageAsset imageAsset;
        if (database_.getImageAsset(uuid, imageAsset)) {
            // Convert RGBA8 LINEAR to sRGB (RGB8 stays as-is since there's no RGB8_SRGB format)
            if (imageAsset.format == ImageFormat::RGBA8) {
                LOG_INFO("GLTFLoader", "Converting image %s from RGBA8 to RGBA8_SRGB", uuid.c_str());
                imageAsset.format = ImageFormat::RGBA8_SRGB;
                database_.storeImageAsset(imageAsset);  // Update in database
            } else {
                LOG_INFO("GLTFLoader", "Image %s format is %d (not RGBA8, skipping)", uuid.c_str(), (int)imageAsset.format);
            }
        } else {
            LOG_WARN("GLTFLoader", "Failed to get image asset %s for sRGB update", uuid.c_str());
        }
    }

    return true;
}

bool GLTFLoader::loadMeshes(void* gltfModelPtr, ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    for (size_t meshIdx = 0; meshIdx < gltfModel->meshes.size(); meshIdx++) {
        const tinygltf::Mesh& gltfMesh = gltfModel->meshes[meshIdx];

        // Load ALL primitives (each primitive becomes a separate MeshAsset)
        if (gltfMesh.primitives.empty()) {
            LOG_WARN("GLTFLoader", "Mesh %zu has no primitives", meshIdx);
            continue;
        }

        // Iterate through all primitives in this mesh
        for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); primIdx++) {
            const tinygltf::Primitive& gltfPrim = gltfMesh.primitives[primIdx];

            MeshAsset mesh;
            mesh.uuid = AssetDatabase::generateUuid();

            // Name includes primitive index if multiple primitives
            if (gltfMesh.primitives.size() > 1) {
                mesh.name = gltfMesh.name.empty()
                    ? ("Mesh_" + std::to_string(meshIdx) + "_Prim_" + std::to_string(primIdx))
                    : (gltfMesh.name + "_Prim_" + std::to_string(primIdx));
            } else {
                mesh.name = gltfMesh.name.empty() ? ("Mesh_" + std::to_string(meshIdx)) : gltfMesh.name;
            }
        // Check if this is a skinned mesh (has joint/weight attributes)
        bool hasSkinning = gltfPrim.attributes.find("JOINTS_0") != gltfPrim.attributes.end() &&
                          gltfPrim.attributes.find("WEIGHTS_0") != gltfPrim.attributes.end();

        mesh.vertexFormat = hasSkinning ? VertexFormat::VERTEX_3D_SKINNED : VertexFormat::VERTEX_3D_LIT;
        mesh.vertexStride = MeshAsset::getVertexStride(mesh.vertexFormat);

        // Extract vertex attributes
        std::vector<float> positions, normals, texcoords, tangents;

        // Position (required)
        if (gltfPrim.attributes.find("POSITION") != gltfPrim.attributes.end()) {
            int accessorIdx = gltfPrim.attributes.at("POSITION");
            const tinygltf::Accessor& accessor = gltfModel->accessors[accessorIdx];
            if (!extractVertexAttribute(gltfModelPtr, accessorIdx, positions, accessor.count * 3)) {
                LOG_ERROR("GLTFLoader", "Failed to extract positions");
                return false;
            }
            mesh.vertexCount = static_cast<uint32_t>(accessor.count);
        } else {
            LOG_ERROR("GLTFLoader", "Mesh %zu has no POSITION attribute", meshIdx);
            return false;
        }

        // Normal
        if (gltfPrim.attributes.find("NORMAL") != gltfPrim.attributes.end()) {
            extractVertexAttribute(gltfModelPtr, gltfPrim.attributes.at("NORMAL"), normals, mesh.vertexCount * 3);
        } else {
            normals.resize(mesh.vertexCount * 3, 0.0f);
            // Set default normals pointing up
            for (uint32_t i = 0; i < mesh.vertexCount; i++) {
                normals[i * 3 + 2] = 1.0f;
            }
        }

        // Texcoord
        if (gltfPrim.attributes.find("TEXCOORD_0") != gltfPrim.attributes.end()) {
            extractVertexAttribute(gltfModelPtr, gltfPrim.attributes.at("TEXCOORD_0"), texcoords, mesh.vertexCount * 2);
            // Debug: Print first few UV coordinates
            LOG_INFO("GLTFLoader", "Loaded %zu texcoords. First 5 UVs:", texcoords.size() / 2);
            for (size_t i = 0; i < std::min(size_t(5), texcoords.size() / 2); i++) {
                LOG_INFO("GLTFLoader", "  UV[%zu] = (%.4f, %.4f)", i, texcoords[i*2], texcoords[i*2+1]);
            }
        } else {
            texcoords.resize(mesh.vertexCount * 2, 0.0f);
        }

        // Extract indices BEFORE tangent generation (needed for proper tangent calculation)
        if (gltfPrim.indices >= 0) {
            if (!extractIndexData(gltfModelPtr, gltfPrim.indices, mesh.indexData)) {
                LOG_ERROR("GLTFLoader", "Failed to extract indices");
                return false;
            }
            mesh.indexCount = static_cast<uint32_t>(mesh.indexData.size());
        }

        // Tangent (vec4 with handedness in w)
        if (gltfPrim.attributes.find("TANGENT") != gltfPrim.attributes.end()) {
            extractVertexAttribute(gltfModelPtr, gltfPrim.attributes.at("TANGENT"), tangents, mesh.vertexCount * 4);
            LOG_INFO("GLTFLoader", "Using tangents from GLTF file");
        } else {
            // Generate tangents using MikkTSpace-like algorithm
            LOG_INFO("GLTFLoader", "Generating tangents from geometry and UVs");
            generateTangents(positions, normals, texcoords, mesh.indexData, tangents, mesh.vertexCount);
        }

        // Interleave vertex data: pos[3] + normal[3] + texCoord[2] + tangent[4]
        mesh.vertexData.reserve(mesh.vertexCount * 12);  // 12 floats per vertex
        for (uint32_t i = 0; i < mesh.vertexCount; i++) {
            // Position
            mesh.vertexData.push_back(positions[i * 3 + 0]);
            mesh.vertexData.push_back(positions[i * 3 + 1]);
            mesh.vertexData.push_back(positions[i * 3 + 2]);
            // Normal
            mesh.vertexData.push_back(normals[i * 3 + 0]);
            mesh.vertexData.push_back(normals[i * 3 + 1]);
            mesh.vertexData.push_back(normals[i * 3 + 2]);
            // TexCoord
            mesh.vertexData.push_back(texcoords[i * 2 + 0]);
            mesh.vertexData.push_back(texcoords[i * 2 + 1]);
            // Tangent (vec4)
            mesh.vertexData.push_back(tangents[i * 4 + 0]);
            mesh.vertexData.push_back(tangents[i * 4 + 1]);
            mesh.vertexData.push_back(tangents[i * 4 + 2]);
            mesh.vertexData.push_back(tangents[i * 4 + 3]);
        }

        // Extract skeletal animation data (joint indices and weights)
        if (hasSkinning) {
            // Extract JOINTS_0 (bone indices, usually VEC4 of UNSIGNED_BYTE or UNSIGNED_SHORT)
            int jointsAccessorIdx = gltfPrim.attributes.at("JOINTS_0");
            const tinygltf::Accessor& jointsAccessor = gltfModel->accessors[jointsAccessorIdx];
            const tinygltf::BufferView& jointsBufferView = gltfModel->bufferViews[jointsAccessor.bufferView];
            const tinygltf::Buffer& jointsBuffer = gltfModel->buffers[jointsBufferView.buffer];
            const uint8_t* jointsData = &jointsBuffer.data[jointsBufferView.byteOffset + jointsAccessor.byteOffset];

            // Pre-allocate joint indices (4 per vertex)
            mesh.jointIndices.resize(mesh.vertexCount * 4);

            size_t jointsStride = jointsAccessor.ByteStride(jointsBufferView);
            for (uint32_t i = 0; i < mesh.vertexCount; i++) {
                const uint8_t* jointPtr = jointsData + i * jointsStride;

                if (jointsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    mesh.jointIndices[i * 4 + 0] = jointPtr[0];
                    mesh.jointIndices[i * 4 + 1] = jointPtr[1];
                    mesh.jointIndices[i * 4 + 2] = jointPtr[2];
                    mesh.jointIndices[i * 4 + 3] = jointPtr[3];
                } else if (jointsAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* jointPtr16 = reinterpret_cast<const uint16_t*>(jointPtr);
                    mesh.jointIndices[i * 4 + 0] = static_cast<uint8_t>(jointPtr16[0]);
                    mesh.jointIndices[i * 4 + 1] = static_cast<uint8_t>(jointPtr16[1]);
                    mesh.jointIndices[i * 4 + 2] = static_cast<uint8_t>(jointPtr16[2]);
                    mesh.jointIndices[i * 4 + 3] = static_cast<uint8_t>(jointPtr16[3]);
                }
            }

            // Extract WEIGHTS_0 (bone weights, usually VEC4 of FLOAT)
            int weightsAccessorIdx = gltfPrim.attributes.at("WEIGHTS_0");
            const tinygltf::Accessor& weightsAccessor = gltfModel->accessors[weightsAccessorIdx];
            const tinygltf::BufferView& weightsBufferView = gltfModel->bufferViews[weightsAccessor.bufferView];
            const tinygltf::Buffer& weightsBuffer = gltfModel->buffers[weightsBufferView.buffer];
            const uint8_t* weightsData = &weightsBuffer.data[weightsBufferView.byteOffset + weightsAccessor.byteOffset];

            // Pre-allocate joint weights (4 per vertex)
            mesh.jointWeights.resize(mesh.vertexCount * 4);

            size_t weightsStride = weightsAccessor.ByteStride(weightsBufferView);
            for (uint32_t i = 0; i < mesh.vertexCount; i++) {
                const float* weightPtr = reinterpret_cast<const float*>(weightsData + i * weightsStride);
                mesh.jointWeights[i * 4 + 0] = weightPtr[0];
                mesh.jointWeights[i * 4 + 1] = weightPtr[1];
                mesh.jointWeights[i * 4 + 2] = weightPtr[2];
                mesh.jointWeights[i * 4 + 3] = weightPtr[3];

                // Normalize weights (GLTF spec allows non-normalized)
                float sum = mesh.jointWeights[i * 4 + 0] + mesh.jointWeights[i * 4 + 1] +
                           mesh.jointWeights[i * 4 + 2] + mesh.jointWeights[i * 4 + 3];
                if (sum > 0.0001f && std::abs(sum - 1.0f) > 0.0001f) {
                    float invSum = 1.0f / sum;
                    mesh.jointWeights[i * 4 + 0] *= invSum;
                    mesh.jointWeights[i * 4 + 1] *= invSum;
                    mesh.jointWeights[i * 4 + 2] *= invSum;
                    mesh.jointWeights[i * 4 + 3] *= invSum;
                }
            }

            LOG_INFO("GLTFLoader", "Loaded skinning data: %u vertices with 4 bone influences each",
                     mesh.vertexCount);
        }

        // Material reference
        if (gltfPrim.material >= 0 && gltfPrim.material < static_cast<int>(outModel.materials.size())) {
            mesh.materialUuid = outModel.materials[gltfPrim.material].uuid;
        }

        // Compute AABB (simple min/max)
        if (!positions.empty()) {
            mesh.aabbMin[0] = mesh.aabbMin[1] = mesh.aabbMin[2] = FLT_MAX;
            mesh.aabbMax[0] = mesh.aabbMax[1] = mesh.aabbMax[2] = -FLT_MAX;
            for (uint32_t i = 0; i < mesh.vertexCount; i++) {
                for (int j = 0; j < 3; j++) {
                    float val = positions[i * 3 + j];
                    if (val < mesh.aabbMin[j]) mesh.aabbMin[j] = val;
                    if (val > mesh.aabbMax[j]) mesh.aabbMax[j] = val;
                }
            }
        }

        // Store mesh UUID for reference
        std::string meshUuid = database_.storeMeshAsset(mesh);
        mesh.uuid = meshUuid;

        outModel.meshes.push_back(mesh);

        LOG_INFO("GLTFLoader", "Loaded primitive %zu of mesh %zu: %s (%u vertices, %u indices%s)",
                 primIdx, meshIdx, mesh.name.c_str(), mesh.vertexCount, mesh.indexCount,
                 hasSkinning ? ", skinned" : "");
        } // End primitive loop
    }

    return true;
}

bool GLTFLoader::loadNodes(void* gltfModelPtr, ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    if (gltfModel->scenes.empty()) {
        LOG_ERROR("GLTFLoader", "No scenes in GLTF file");
        return false;
    }

    // Load default scene
    int sceneIdx = gltfModel->defaultScene >= 0 ? gltfModel->defaultScene : 0;
    const tinygltf::Scene& scene = gltfModel->scenes[sceneIdx];

    // Load all nodes
    outModel.nodes.resize(gltfModel->nodes.size());
    for (size_t i = 0; i < gltfModel->nodes.size(); i++) {
        const tinygltf::Node& gltfNode = gltfModel->nodes[i];
        ModelNode& node = outModel.nodes[i];

        node.name = gltfNode.name;
        node.meshIndex = gltfNode.mesh;
        node.skinIndex = gltfNode.skin;  // Skin binding for skeletal animation
        node.parentIndex = -1;  // Will be set below

        // Load transform
        if (!gltfNode.matrix.empty()) {
            // Matrix form
            for (int j = 0; j < 16; j++) {
                node.localTransform[j] = static_cast<float>(gltfNode.matrix[j]);
            }
        } else {
            // TRS form (translation, rotation, scale)
            // For simplicity, just use identity for now
            // TODO: Compute matrix from TRS
            for (int j = 0; j < 16; j++) {
                node.localTransform[j] = (j % 5 == 0) ? 1.0f : 0.0f;
            }
        }

        // Set children parent indices
        for (int childIdx : gltfNode.children) {
            if (childIdx >= 0 && childIdx < static_cast<int>(outModel.nodes.size())) {
                outModel.nodes[childIdx].parentIndex = static_cast<int32_t>(i);
                node.childIndices.push_back(childIdx);
            }
        }
    }

    // Store root node indices
    for (int rootIdx : scene.nodes) {
        if (rootIdx >= 0 && rootIdx < static_cast<int>(outModel.nodes.size())) {
            outModel.rootNodeIndices.push_back(rootIdx);
        }
    }

    LOG_INFO("GLTFLoader", "Loaded scene graph: %zu nodes (%zu roots)",
             outModel.nodes.size(), outModel.rootNodeIndices.size());

    return true;
}

bool GLTFLoader::extractVertexAttribute(void* gltfModelPtr, int accessorIndex,
                                       std::vector<float>& outData, size_t expectedCount) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(gltfModel->accessors.size())) {
        return false;
    }

    const tinygltf::Accessor& accessor = gltfModel->accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel->buffers[bufferView.buffer];

    const uint8_t* data = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

    outData.reserve(expectedCount);

    // Only support float data for now
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        const float* floatData = reinterpret_cast<const float*>(data);
        size_t stride = accessor.ByteStride(bufferView);
        size_t componentCount = tinygltf::GetNumComponentsInType(accessor.type);

        for (size_t i = 0; i < accessor.count; i++) {
            const float* vertex = reinterpret_cast<const float*>(data + i * stride);
            for (size_t j = 0; j < componentCount; j++) {
                outData.push_back(vertex[j]);
            }
        }
    }

    return true;
}

bool GLTFLoader::extractIndexData(void* gltfModelPtr, int accessorIndex,
                                  std::vector<uint32_t>& outIndices) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(gltfModel->accessors.size())) {
        return false;
    }

    const tinygltf::Accessor& accessor = gltfModel->accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel->buffers[bufferView.buffer];

    const uint8_t* data = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

    outIndices.reserve(accessor.count);

    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const uint32_t* indices = reinterpret_cast<const uint32_t*>(data);
            for (size_t i = 0; i < accessor.count; i++) {
                outIndices.push_back(indices[i]);
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const uint16_t* indices = reinterpret_cast<const uint16_t*>(data);
            for (size_t i = 0; i < accessor.count; i++) {
                outIndices.push_back(static_cast<uint32_t>(indices[i]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            for (size_t i = 0; i < accessor.count; i++) {
                outIndices.push_back(static_cast<uint32_t>(data[i]));
            }
            break;
        }
        default:
            LOG_ERROR("GLTFLoader", "Unsupported index component type");
            return false;
    }

    return true;
}

void GLTFLoader::generateTangents(const std::vector<float>& positions,
                                  const std::vector<float>& normals,
                                  const std::vector<float>& texcoords,
                                  const std::vector<uint32_t>& indices,
                                  std::vector<float>& outTangents,
                                  uint32_t vertexCount) {
    // Initialize tangent and bitangent accumulators
    std::vector<float> tangents(vertexCount * 3, 0.0f);
    std::vector<float> bitangents(vertexCount * 3, 0.0f);

    outTangents.resize(vertexCount * 4, 0.0f);

    // If no indices, can't generate tangents properly
    if (indices.empty()) {
        LOG_WARN("GLTFLoader", "Cannot generate tangents without indices, using defaults");
        for (uint32_t i = 0; i < vertexCount; i++) {
            outTangents[i * 4 + 0] = 1.0f;  // X
            outTangents[i * 4 + 1] = 0.0f;  // Y
            outTangents[i * 4 + 2] = 0.0f;  // Z
            outTangents[i * 4 + 3] = 1.0f;  // W (handedness)
        }
        return;
    }

    // Process each triangle
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        // Positions
        float v0x = positions[i0 * 3 + 0], v0y = positions[i0 * 3 + 1], v0z = positions[i0 * 3 + 2];
        float v1x = positions[i1 * 3 + 0], v1y = positions[i1 * 3 + 1], v1z = positions[i1 * 3 + 2];
        float v2x = positions[i2 * 3 + 0], v2y = positions[i2 * 3 + 1], v2z = positions[i2 * 3 + 2];

        // Texture coordinates
        float w0x = texcoords[i0 * 2 + 0], w0y = texcoords[i0 * 2 + 1];
        float w1x = texcoords[i1 * 2 + 0], w1y = texcoords[i1 * 2 + 1];
        float w2x = texcoords[i2 * 2 + 0], w2y = texcoords[i2 * 2 + 1];

        // Position deltas
        float e1x = v1x - v0x, e1y = v1y - v0y, e1z = v1z - v0z;
        float e2x = v2x - v0x, e2y = v2y - v0y, e2z = v2z - v0z;

        // UV deltas
        float x1 = w1x - w0x, y1 = w1y - w0y;
        float x2 = w2x - w0x, y2 = w2y - w0y;

        // Compute tangent and bitangent using UV gradient method
        float r = (x1 * y2 - x2 * y1);

        // Avoid division by zero
        if (r != 0.0f) {
            r = 1.0f / r;

            float tx = (e1x * y2 - e2x * y1) * r;
            float ty = (e1y * y2 - e2y * y1) * r;
            float tz = (e1z * y2 - e2z * y1) * r;

            float bx = (e2x * x1 - e1x * x2) * r;
            float by = (e2y * x1 - e1y * x2) * r;
            float bz = (e2z * x1 - e1z * x2) * r;

            // Accumulate tangent and bitangent for each vertex of the triangle
            for (int j = 0; j < 3; j++) {
                uint32_t idx = indices[i + j];
                tangents[idx * 3 + 0] += tx;
                tangents[idx * 3 + 1] += ty;
                tangents[idx * 3 + 2] += tz;
                bitangents[idx * 3 + 0] += bx;
                bitangents[idx * 3 + 1] += by;
                bitangents[idx * 3 + 2] += bz;
            }
        }
    }

    // Orthogonalize and normalize tangents using Gram-Schmidt
    for (uint32_t i = 0; i < vertexCount; i++) {
        // Normal
        float nx = normals[i * 3 + 0];
        float ny = normals[i * 3 + 1];
        float nz = normals[i * 3 + 2];

        // Tangent
        float tx = tangents[i * 3 + 0];
        float ty = tangents[i * 3 + 1];
        float tz = tangents[i * 3 + 2];

        // Bitangent
        float bx = bitangents[i * 3 + 0];
        float by = bitangents[i * 3 + 1];
        float bz = bitangents[i * 3 + 2];

        // Gram-Schmidt orthogonalize: tangent = tangent - normal * dot(normal, tangent)
        float dot = nx * tx + ny * ty + nz * tz;
        tx -= nx * dot;
        ty -= ny * dot;
        tz -= nz * dot;

        // Normalize tangent
        float tlen = sqrtf(tx * tx + ty * ty + tz * tz);
        if (tlen > 0.0001f) {
            tx /= tlen;
            ty /= tlen;
            tz /= tlen;
        } else {
            // Fallback if tangent is zero
            tx = 1.0f;
            ty = 0.0f;
            tz = 0.0f;
        }

        // Calculate handedness (sign of cross(normal, tangent) dot bitangent)
        float cx = ny * tz - nz * ty;
        float cy = nz * tx - nx * tz;
        float cz = nx * ty - ny * tx;
        float handedness = (cx * bx + cy * by + cz * bz) < 0.0f ? -1.0f : 1.0f;

        // Store tangent as vec4 (xyz = tangent, w = handedness)
        outTangents[i * 4 + 0] = tx;
        outTangents[i * 4 + 1] = ty;
        outTangents[i * 4 + 2] = tz;
        outTangents[i * 4 + 3] = handedness;
    }
}

bool GLTFLoader::loadSkins(void* gltfModelPtr, ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    outModel.skins.reserve(gltfModel->skins.size());

    for (size_t skinIdx = 0; skinIdx < gltfModel->skins.size(); skinIdx++) {
        const tinygltf::Skin& gltfSkin = gltfModel->skins[skinIdx];
        SkinAsset skin;

        skin.name = gltfSkin.name.empty() ? ("Skin_" + std::to_string(skinIdx)) : gltfSkin.name;
        skin.skeletonRootIndex = gltfSkin.skeleton;

        // Copy joint node indices
        skin.jointNodeIndices.reserve(gltfSkin.joints.size());
        for (int jointIdx : gltfSkin.joints) {
            skin.jointNodeIndices.push_back(jointIdx);
        }

        // Extract inverse bind matrices from accessor
        if (gltfSkin.inverseBindMatrices >= 0) {
            const tinygltf::Accessor& accessor = gltfModel->accessors[gltfSkin.inverseBindMatrices];
            const tinygltf::BufferView& bufferView = gltfModel->bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = gltfModel->buffers[bufferView.buffer];

            const uint8_t* data = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

            skin.inverseBindMatrices.resize(accessor.count);

            // Inverse bind matrices are stored as mat4 (16 floats each)
            for (size_t i = 0; i < accessor.count; i++) {
                const float* matData = reinterpret_cast<const float*>(data + i * accessor.ByteStride(bufferView));
                for (int j = 0; j < 16; j++) {
                    skin.inverseBindMatrices[i][j] = matData[j];
                }
            }
        } else {
            // If no inverse bind matrices provided, use identity
            skin.inverseBindMatrices.resize(gltfSkin.joints.size());
            for (auto& mat : skin.inverseBindMatrices) {
                for (int j = 0; j < 16; j++) {
                    mat[j] = (j % 5 == 0) ? 1.0f : 0.0f;  // Identity matrix
                }
            }
        }

        outModel.skins.push_back(std::move(skin));

        LOG_INFO("GLTFLoader", "Loaded skin %zu: %s (%zu joints)",
                 skinIdx, outModel.skins.back().name.c_str(),
                 outModel.skins.back().jointNodeIndices.size());
    }

    return true;
}

bool GLTFLoader::loadAnimations(void* gltfModelPtr, ModelAsset& outModel) {
    tinygltf::Model* gltfModel = static_cast<tinygltf::Model*>(gltfModelPtr);

    outModel.animations.reserve(gltfModel->animations.size());

    for (size_t animIdx = 0; animIdx < gltfModel->animations.size(); animIdx++) {
        const tinygltf::Animation& gltfAnim = gltfModel->animations[animIdx];

        AnimationClipRaw clip;
        clip.name = gltfAnim.name.empty()
            ? ("Animation_" + std::to_string(animIdx))
            : gltfAnim.name;

        // Load samplers (keyframe data)
        clip.samplers.reserve(gltfAnim.samplers.size());
        for (size_t samplerIdx = 0; samplerIdx < gltfAnim.samplers.size(); samplerIdx++) {
            const tinygltf::AnimationSampler& gltfSampler = gltfAnim.samplers[samplerIdx];
            AnimationSamplerRaw sampler;

            // Set interpolation mode
            if (gltfSampler.interpolation == "STEP") {
                sampler.interpolation = AnimationInterpolation::Step;
            } else if (gltfSampler.interpolation == "CUBICSPLINE") {
                sampler.interpolation = AnimationInterpolation::CubicSpline;
            } else {
                sampler.interpolation = AnimationInterpolation::Linear;
            }

            // Extract input (keyframe times)
            if (gltfSampler.input >= 0) {
                const tinygltf::Accessor& timeAccessor = gltfModel->accessors[gltfSampler.input];
                const tinygltf::BufferView& timeBufferView = gltfModel->bufferViews[timeAccessor.bufferView];
                const tinygltf::Buffer& timeBuffer = gltfModel->buffers[timeBufferView.buffer];

                const uint8_t* timeData = &timeBuffer.data[timeBufferView.byteOffset + timeAccessor.byteOffset];
                sampler.times.resize(timeAccessor.count);

                for (size_t i = 0; i < timeAccessor.count; i++) {
                    const float* t = reinterpret_cast<const float*>(timeData + i * timeAccessor.ByteStride(timeBufferView));
                    sampler.times[i] = *t;
                }
            }

            // Extract output (keyframe values)
            if (gltfSampler.output >= 0) {
                const tinygltf::Accessor& valueAccessor = gltfModel->accessors[gltfSampler.output];
                const tinygltf::BufferView& valueBufferView = gltfModel->bufferViews[valueAccessor.bufferView];
                const tinygltf::Buffer& valueBuffer = gltfModel->buffers[valueBufferView.buffer];

                const uint8_t* valueData = &valueBuffer.data[valueBufferView.byteOffset + valueAccessor.byteOffset];
                size_t componentCount = tinygltf::GetNumComponentsInType(valueAccessor.type);
                size_t stride = valueAccessor.ByteStride(valueBufferView);

                // For cubic spline, output contains in-tangent, value, out-tangent
                if (sampler.interpolation == AnimationInterpolation::CubicSpline) {
                    // Each keyframe has 3 sets of values: inTangent, value, outTangent
                    size_t keyframeCount = sampler.times.size();
                    sampler.inTangents.resize(keyframeCount * componentCount);
                    sampler.values.resize(keyframeCount * componentCount);
                    sampler.outTangents.resize(keyframeCount * componentCount);

                    for (size_t i = 0; i < keyframeCount; i++) {
                        const float* inTan = reinterpret_cast<const float*>(valueData + (i * 3 + 0) * stride);
                        const float* value = reinterpret_cast<const float*>(valueData + (i * 3 + 1) * stride);
                        const float* outTan = reinterpret_cast<const float*>(valueData + (i * 3 + 2) * stride);

                        for (size_t j = 0; j < componentCount; j++) {
                            sampler.inTangents[i * componentCount + j] = inTan[j];
                            sampler.values[i * componentCount + j] = value[j];
                            sampler.outTangents[i * componentCount + j] = outTan[j];
                        }
                    }
                } else {
                    // Linear or Step: just values
                    sampler.values.resize(valueAccessor.count * componentCount);
                    for (size_t i = 0; i < valueAccessor.count; i++) {
                        const float* v = reinterpret_cast<const float*>(valueData + i * stride);
                        for (size_t j = 0; j < componentCount; j++) {
                            sampler.values[i * componentCount + j] = v[j];
                        }
                    }
                }
            }

            clip.samplers.push_back(std::move(sampler));
        }

        // Load channels (animation targets)
        clip.channels.reserve(gltfAnim.channels.size());
        for (size_t channelIdx = 0; channelIdx < gltfAnim.channels.size(); channelIdx++) {
            const tinygltf::AnimationChannel& gltfChannel = gltfAnim.channels[channelIdx];
            AnimationChannelRaw channel;

            channel.samplerIndex = static_cast<uint32_t>(gltfChannel.sampler);
            channel.targetNodeIndex = gltfChannel.target_node;

            // Determine animation path
            if (gltfChannel.target_path == "translation") {
                channel.path = AnimationPath::Translation;
            } else if (gltfChannel.target_path == "rotation") {
                channel.path = AnimationPath::Rotation;
            } else if (gltfChannel.target_path == "scale") {
                channel.path = AnimationPath::Scale;
            } else {
                // Skip unsupported paths (weights, etc.)
                LOG_WARN("GLTFLoader", "Skipping unsupported animation path: %s",
                         gltfChannel.target_path.c_str());
                continue;
            }

            clip.channels.push_back(channel);
        }

        // Calculate total duration
        clip.updateDuration();

        LOG_INFO("GLTFLoader", "Loaded animation %zu: %s (%.2fs, %zu channels, %zu samplers)",
                 animIdx, clip.name.c_str(), clip.duration,
                 clip.channels.size(), clip.samplers.size());

        outModel.animations.push_back(std::move(clip));

        // Generate UUID for reference
        std::string animUuid = AssetDatabase::generateUuid();
        outModel.animationUuids.push_back(animUuid);
    }

    return true;
}

} // namespace assets
} // namespace jupiter
