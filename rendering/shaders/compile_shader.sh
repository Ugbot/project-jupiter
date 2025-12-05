#!/bin/bash
# Compile GLSL shaders with Jupiter's include system
#
# Usage: ./compile_shader.sh <input_shader> [output_shader]
#
# Example: ./compile_shader.sh pbr/pbr.frag pbr/pbr.frag.spv

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INCLUDE_DIR="${SCRIPT_DIR}"

if [ -z "$1" ]; then
    echo "Usage: $0 <input_shader> [output_shader]"
    echo ""
    echo "Compiles a GLSL shader with Jupiter's include system."
    echo "Include files should use: #include <includes/filename.glsl>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="${2:-${INPUT_FILE}.spv}"

# Check if input file exists
if [ ! -f "$INPUT_FILE" ]; then
    # Try relative to script directory
    if [ -f "${SCRIPT_DIR}/${INPUT_FILE}" ]; then
        INPUT_FILE="${SCRIPT_DIR}/${INPUT_FILE}"
    else
        echo "Error: Input file not found: $INPUT_FILE"
        exit 1
    fi
fi

# Find glslangValidator
GLSL_VALIDATOR=$(which glslangValidator 2>/dev/null)
if [ -z "$GLSL_VALIDATOR" ]; then
    # Try common locations
    if [ -f "/usr/local/bin/glslangValidator" ]; then
        GLSL_VALIDATOR="/usr/local/bin/glslangValidator"
    elif [ -f "$VULKAN_SDK/bin/glslangValidator" ]; then
        GLSL_VALIDATOR="$VULKAN_SDK/bin/glslangValidator"
    else
        echo "Error: glslangValidator not found. Please install the Vulkan SDK."
        exit 1
    fi
fi

echo "Compiling: $INPUT_FILE -> $OUTPUT_FILE"
echo "Include path: $INCLUDE_DIR"

# Compile with includes
"$GLSL_VALIDATOR" -V "$INPUT_FILE" -o "$OUTPUT_FILE" -I"$INCLUDE_DIR"
RESULT=$?

if [ $RESULT -eq 0 ]; then
    echo "Success: $OUTPUT_FILE"
else
    echo "Error: Compilation failed with code $RESULT"
fi

exit $RESULT

