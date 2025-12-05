// sRGB to Linear conversion
// Ported from Sascha Willems' vulkan-gltf-pbr (MIT License)

vec4 SRGBtoLINEAR(vec4 srgbIn) {
    // Use precise sRGB formula (not the fast 2.2 approximation)
    vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
    vec3 linOut = mix(
        srgbIn.xyz / vec3(12.92),
        pow((srgbIn.xyz + vec3(0.055)) / vec3(1.055), vec3(2.4)),
        bLess
    );
    return vec4(linOut, srgbIn.w);
}

vec3 SRGBtoLINEAR(vec3 srgbIn) {
    return SRGBtoLINEAR(vec4(srgbIn, 1.0)).rgb;
}
