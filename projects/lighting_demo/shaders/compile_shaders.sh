#!/bin/bash

# Shader compilation script for lighting demo
# Requires glslangValidator (from Vulkan SDK)

SHADER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$SHADER_DIR"

echo "Compiling shaders in $SHADER_DIR..."

# Check if glslangValidator is available
if ! command -v glslangValidator &> /dev/null; then
    echo "Error: glslangValidator not found. Please install Vulkan SDK."
    exit 1
fi

# Compile vertex shaders
echo "Compiling lighting.vert..."
glslangValidator -V "$SHADER_DIR/lighting.vert" -o "$OUTPUT_DIR/lighting.vert.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling lighting.vert"
    exit 1
fi

echo "Compiling scene.vert..."
glslangValidator -V "$SHADER_DIR/scene.vert" -o "$OUTPUT_DIR/scene.vert.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling scene.vert"
    exit 1
fi

# Compile fragment shaders
echo "Compiling lighting.frag..."
glslangValidator -V "$SHADER_DIR/lighting.frag" -o "$OUTPUT_DIR/lighting.frag.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling lighting.frag"
    exit 1
fi

echo "Compiling scene.frag..."
glslangValidator -V "$SHADER_DIR/scene.frag" -o "$OUTPUT_DIR/scene.frag.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling scene.frag"
    exit 1
fi

echo "Compiling pbr.vert..."
glslangValidator -V "$SHADER_DIR/pbr.vert" -o "$OUTPUT_DIR/pbr.vert.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling pbr.vert"
    exit 1
fi

echo "Compiling pbr.frag..."
glslangValidator -V "$SHADER_DIR/pbr.frag" -o "$OUTPUT_DIR/pbr.frag.spv"
if [ $? -ne 0 ]; then
    echo "Error compiling pbr.frag"
    exit 1
fi

echo "Shader compilation complete!"
echo "  - lighting.vert.spv"
echo "  - lighting.frag.spv"
echo "  - scene.vert.spv"
echo "  - scene.frag.spv"
echo "  - pbr.vert.spv"
echo "  - pbr.frag.spv"
