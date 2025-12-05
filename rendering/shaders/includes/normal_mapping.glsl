/**
 * @file normal_mapping.glsl
 * @brief Normal mapping utilities
 * 
 * Functions for calculating tangent-space normals from normal maps.
 * 
 * Usage: #include <includes/normal_mapping.glsl>
 */

#ifndef NORMAL_MAPPING_GLSL
#define NORMAL_MAPPING_GLSL

/**
 * @brief Calculate TBN matrix and transform normal from tangent space
 * 
 * Uses the fragment's geometric normal, tangent, and texture normal
 * to compute the world-space perturbed normal.
 * 
 * @param normalMap Normal map sampler
 * @param texCoord Texture coordinates
 * @param geometricNormal Interpolated vertex normal
 * @param tangent Interpolated vertex tangent (xyz) with bitangent sign (w)
 * @return World-space perturbed normal
 */
vec3 getNormalFromMap(
    sampler2D normalMap,
    vec2 texCoord,
    vec3 geometricNormal,
    vec4 tangent
) {
    vec3 tangentNormal = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(geometricNormal);
    vec3 T = normalize(tangent.xyz);
    T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalization
    vec3 B = cross(N, T) * tangent.w;  // Handedness from tangent.w
    
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

/**
 * @brief Calculate TBN matrix from position derivatives (no tangent attribute)
 * 
 * Use when tangent attributes are not available.
 * Less accurate but works with any geometry.
 * 
 * @param normalMap Normal map sampler
 * @param texCoord Texture coordinates
 * @param worldPos World position
 * @param geometricNormal Geometric normal
 * @return World-space perturbed normal
 */
vec3 getNormalFromMapDerivatives(
    sampler2D normalMap,
    vec2 texCoord,
    vec3 worldPos,
    vec3 geometricNormal
) {
    vec3 tangentNormal = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(geometricNormal);
    
    // Calculate tangent and bitangent from derivatives
    vec3 dPdx = dFdx(worldPos);
    vec3 dPdy = dFdy(worldPos);
    vec2 dUVdx = dFdx(texCoord);
    vec2 dUVdy = dFdy(texCoord);
    
    vec3 dPdU = dUVdy.y * dPdx - dUVdx.y * dPdy;
    vec3 dPdV = dUVdx.x * dPdy - dUVdy.x * dPdx;
    
    float invMax = inversesqrt(max(dot(dPdU, dPdU), dot(dPdV, dPdV)));
    vec3 T = dPdU * invMax;
    vec3 B = dPdV * invMax;
    
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

/**
 * @brief Blend two normals in tangent space
 * 
 * Useful for detail normal maps or blending normal maps.
 * 
 * @param n1 First normal (tangent space)
 * @param n2 Second normal (tangent space)
 * @return Blended normal
 */
vec3 blendNormals(vec3 n1, vec3 n2) {
    // Reoriented Normal Mapping blend
    n1.z += 1.0;
    n2.xy = -n2.xy;
    return normalize(n1 * dot(n1, n2) - n2 * n1.z);
}

#endif // NORMAL_MAPPING_GLSL

