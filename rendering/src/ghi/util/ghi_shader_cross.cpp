/**
 * @file ghi_shader_cross.cpp
 * @brief Shader Cross-Compilation Implementation
 * 
 * Uses SPIRV-Cross library to convert SPIR-V to backend-specific shader languages.
 */

#include "ghi_shader_cross.h"
#include "logging/logging.h"

// SPIRV-Cross includes
#include <spirv_cross.hpp>
#include <spirv_msl.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>

#include <fstream>

namespace jupiter {
namespace rendering {
namespace ghi {

CrossCompiledShader crossCompileShader(const std::vector<uint32_t>& spirv, Backend backend) {
    CrossCompiledShader result;
    
    try {
        switch (backend) {
            case Backend::Metal: {
                // SPIR-V → MSL
                spirv_cross::CompilerMSL msl(spirv);
                
                // Determine shader stage from entry points
                spv::ExecutionModel stage = spv::ExecutionModelVertex;
                auto entryPoints = msl.get_entry_points_and_stages();
                for (auto& ep : entryPoints) {
                    stage = ep.execution_model;
                    // Rename "main" to "vertexMain" or "fragmentMain" based on stage
                    if (ep.execution_model == spv::ExecutionModelVertex) {
                        msl.rename_entry_point(ep.name, "vertexMain", ep.execution_model);
                        LOG_INFO("ShaderCross", "Renamed vertex entry point '%s' -> 'vertexMain'", ep.name.c_str());
                    } else if (ep.execution_model == spv::ExecutionModelFragment) {
                        msl.rename_entry_point(ep.name, "fragmentMain", ep.execution_model);
                        LOG_INFO("ShaderCross", "Renamed fragment entry point '%s' -> 'fragmentMain'", ep.name.c_str());
                    }
                }
                
                // Set MSL options
                spirv_cross::CompilerMSL::Options opts;
                opts.platform = spirv_cross::CompilerMSL::Options::macOS;  // or iOS
                opts.enable_decoration_binding = true;
                msl.set_msl_options(opts);
                
                // Remap push constants to buffer index 1 to avoid conflict with UBO at buffer 0
                // Push constants in SPIR-V don't have set/binding, they use a special resource
                spirv_cross::MSLResourceBinding pushConstantBinding;
                pushConstantBinding.stage = stage;
                pushConstantBinding.desc_set = spirv_cross::kPushConstDescSet;  // Special push constant set
                pushConstantBinding.binding = spirv_cross::kPushConstBinding;   // Special push constant binding
                pushConstantBinding.msl_buffer = 1;  // Map to buffer(1) instead of buffer(0)
                msl.add_msl_resource_binding(pushConstantBinding);
                
                LOG_INFO("ShaderCross", "Remapped push constants to buffer(1)");

                // Remap descriptor set/binding buffers into a single flat MSL buffer index space.
                // Must match GHI_MetalBackend::bindUniformBuffer().
                {
                    const auto resources = msl.get_shader_resources();

                    auto remapBuffer = [&](const spirv_cross::Resource& res) {
                        const uint32_t set = msl.get_decoration(res.id, spv::DecorationDescriptorSet);
                        const uint32_t binding = msl.get_decoration(res.id, spv::DecorationBinding);

                        // Reserve:
                        //  - buffer(0) for set0/binding0 (camera)
                        //  - buffer(1) for push constants (above)
                        const uint32_t mslBuffer =
                            (set == 0 && binding == 0) ? 0u : (2u + set * 8u + binding);

                        spirv_cross::MSLResourceBinding rb{};
                        rb.stage = stage;
                        rb.desc_set = set;
                        rb.binding = binding;
                        rb.msl_buffer = mslBuffer;
                        msl.add_msl_resource_binding(rb);
                    };

                    for (const auto& ubo : resources.uniform_buffers) {
                        remapBuffer(ubo);
                    }

                    for (const auto& sbo : resources.storage_buffers) {
                        remapBuffer(sbo);
                    }
                }
                
                // Compile
                result.source = msl.compile();
                result.success = true;
                
                LOG_INFO("ShaderCross", "SPIR-V → MSL conversion successful");
                
                // Debug: write MSL to file for inspection
                {
                    static int fileCounter = 0;
                    char filename[256];
                    snprintf(filename, sizeof(filename), "/tmp/msl_debug_%d.metal", fileCounter++);
                    FILE* f = fopen(filename, "w");
                    if (f) {
                        fputs(result.source.c_str(), f);
                        fclose(f);
                        LOG_INFO("ShaderCross", "Wrote MSL to %s (%zu bytes)", filename, result.source.size());
                    }
                }
                break;
            }
            
            case Backend::OpenGL: {
                // SPIR-V → GLSL
                spirv_cross::CompilerGLSL glsl(spirv);
                
                // Set GLSL options
                spirv_cross::CompilerGLSL::Options opts;
                opts.version = 410;  // OpenGL 4.1 (macOS compatible)
                opts.es = false;
                opts.enable_420pack_extension = false;  // Compatibility
                glsl.set_common_options(opts);
                
                // Compile
                result.source = glsl.compile();
                result.success = true;
                
                LOG_INFO("ShaderCross", "SPIR-V → GLSL conversion successful");
                break;
            }
            
            case Backend::Vulkan: {
                // Already SPIR-V, just validate
                spirv_cross::Compiler compiler(spirv);
                result.source = "(SPIR-V binary - no conversion needed)";
                result.success = true;
                break;
            }
            
            case Backend::DX12: {
                // SPIR-V → HLSL
                spirv_cross::CompilerHLSL hlsl(spirv);
                
                // Set HLSL options
                spirv_cross::CompilerHLSL::Options opts;
                opts.shader_model = 60;  // SM 6.0
                hlsl.set_hlsl_options(opts);
                
                // Compile
                result.source = hlsl.compile();
                result.success = true;
                
                LOG_INFO("ShaderCross", "SPIR-V → HLSL conversion successful");
                break;
            }
            
            default:
                result.errorMessage = "Unknown backend";
                return result;
        }
        
    } catch (const spirv_cross::CompilerError& e) {
        LOG_ERROR("ShaderCross", "Compilation error: %s", e.what());
        result.success = false;
        result.errorMessage = e.what();
    }
    
    return result;
}

std::string spirvToMSL(const char* spirvPath) {
    // Load SPIR-V file
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("ShaderCross", "Failed to open SPIR-V file: %s", spirvPath);
        return "";
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint32_t> spirv(fileSize / 4);
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    file.close();
    
    // Cross-compile
    auto result = crossCompileShader(spirv, Backend::Metal);
    
    if (!result.success) {
        LOG_ERROR("ShaderCross", "Failed to convert to MSL: %s", result.errorMessage.c_str());
        return "";
    }
    
    LOG_INFO("ShaderCross", "Converted %s to MSL (%zu bytes)", spirvPath, result.source.size());
    return result.source;
}

std::string spirvToGLSL(const char* spirvPath, uint32_t version, bool es) {
    // Load SPIR-V file
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("ShaderCross", "Failed to open SPIR-V file: %s", spirvPath);
        return "";
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint32_t> spirv(fileSize / 4);
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    file.close();
    
    // Cross-compile
    spirv_cross::CompilerGLSL glsl(spirv);
    
    spirv_cross::CompilerGLSL::Options opts;
    opts.version = version;
    opts.es = es;
    glsl.set_common_options(opts);
    
    std::string source = glsl.compile();
    
    LOG_INFO("ShaderCross", "Converted %s to GLSL %u (%zu bytes)", spirvPath, version, source.size());
    return source;
}

std::string spirvToHLSL(const char* spirvPath, uint32_t shaderModel) {
    // Load SPIR-V file
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("ShaderCross", "Failed to open SPIR-V file: %s", spirvPath);
        return "";
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0);
    
    std::vector<uint32_t> spirv(fileSize / 4);
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
    file.close();
    
    // Cross-compile
    spirv_cross::CompilerHLSL hlsl(spirv);
    
    spirv_cross::CompilerHLSL::Options opts;
    opts.shader_model = shaderModel;
    hlsl.set_hlsl_options(opts);
    
    std::string source = hlsl.compile();
    
    LOG_INFO("ShaderCross", "Converted %s to HLSL SM%u (%zu bytes)", spirvPath, shaderModel, source.size());
    return source;
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

