#pragma once

/**
 * @file ghi_shader_cross.h
 * @brief Shader Cross-Compilation using SPIRV-Cross
 * 
 * Converts shaders between backends:
 * - SPIR-V (Vulkan) → MSL (Metal)
 * - SPIR-V (Vulkan) → GLSL (OpenGL)
 * - SPIR-V (Vulkan) → HLSL (DX12)
 * 
 * Allows single shader codebase for all backends.
 */

#include "rendering/ghi/ghi_types.h"
#include <string>
#include <vector>

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief Shader cross-compilation result
 */
struct CrossCompiledShader {
    std::string source;      // Compiled shader source code
    bool success = false;
    std::string errorMessage;
    
    // Reflection data
    struct Resource {
        std::string name;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t size = 0;
    };
    
    std::vector<Resource> uniformBuffers;
    std::vector<Resource> storageBuffers;
    std::vector<Resource> sampledImages;
    std::vector<Resource> storageImages;
};

/**
 * @brief Cross-compile SPIR-V to target shader language
 * 
 * @param spirv SPIR-V binary data
 * @param backend Target backend
 * @return Compiled shader source for target backend
 */
CrossCompiledShader crossCompileShader(const std::vector<uint32_t>& spirv, Backend backend);

/**
 * @brief Cross-compile SPIR-V file to MSL (Metal Shading Language)
 * 
 * @param spirvPath Path to .spv file
 * @param options MSL-specific options
 * @return MSL source code
 */
std::string spirvToMSL(const char* spirvPath);

/**
 * @brief Cross-compile SPIR-V file to GLSL
 * 
 * @param spirvPath Path to .spv file
 * @param version GLSL version (e.g. 330, 410, 450)
 * @param es true for GLSL ES
 * @return GLSL source code
 */
std::string spirvToGLSL(const char* spirvPath, uint32_t version = 450, bool es = false);

/**
 * @brief Cross-compile SPIR-V file to HLSL
 * 
 * @param spirvPath Path to .spv file
 * @param shaderModel Shader model version (e.g. 50, 51, 60)
 * @return HLSL source code
 */
std::string spirvToHLSL(const char* spirvPath, uint32_t shaderModel = 60);

/**
 * @brief Reflect on SPIR-V to extract resource bindings
 * 
 * Useful for automatic descriptor set layout creation.
 * 
 * @param spirv SPIR-V binary
 * @return Reflection data
 */
CrossCompiledShader::Resource reflectSPIRV(const std::vector<uint32_t>& spirv);

} // namespace ghi
} // namespace rendering
} // namespace jupiter

