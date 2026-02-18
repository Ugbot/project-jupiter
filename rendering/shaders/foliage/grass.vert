#version 450

// Grass vertex shader - procedural blade generation
// No vertex buffer; uses gl_VertexIndex and instance data

// Camera (from RenderGlobals, Set 0 Binding 0)
layout(set=0, binding=0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec4 position;  // xyz = camera world pos
    float cameraNear;
    float cameraFar;
} camera;

// Grass instance
struct GrassInstance {
    vec4 pos_height;     // xyz = world pos, w = blade height
    vec4 normal_seed;    // xyz = world normal, w = random seed
    vec4 bend_flatten;   // xy = trail bend dir, z = bend amount, w = flatten amount
};

layout(std430, set=1, binding=0) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Trail textures (for extra detail)
layout(set=2, binding=0) uniform sampler2D trailIntensity;
layout(set=2, binding=1) uniform sampler2D trailDir;

// Push constants
layout(push_constant) uniform GrassPC {
    vec4 windDir_speed;    // xyz = wind direction, w = wind speed
    float time;
    float trailBendScale;
    float _pad0;
    float _pad1;
} pc;

// Outputs
layout(location=0) out vec3 fragNormal;
layout(location=1) out vec3 fragColor;
layout(location=2) out float fragAO;

void main() {
    uint instanceID = gl_InstanceIndex;
    GrassInstance inst = instances[instanceID];
    
    // Blade parameters (6 segments, 2 verts each = 12 verts total)
    const int segsPerBlade = 6;
    int segIdx = gl_VertexIndex / 2;
    int sideIdx = gl_VertexIndex % 2;
    
    float t = float(segIdx) / float(segsPerBlade);  // 0..1 along blade height
    
    // Instance data
    vec3 basePos = inst.pos_height.xyz;
    vec3 up = normalize(inst.normal_seed.xyz);
    float height = inst.pos_height.w;
    float seed = inst.normal_seed.w;
    
    // Trail bend (from instance)
    vec2 trailD = inst.bend_flatten.xy;
    float trailI = inst.bend_flatten.z;
    
    // Wind (time-varying, per-blade seed for variety)
    float windPhase = pc.time * pc.windDir_speed.w + seed * 0.1;
    vec3 windBend = pc.windDir_speed.xyz * sin(windPhase) * 0.3;
    
    // Combined bend direction (wind + trail)
    vec3 trailBend3D = vec3(trailD.x, 0.0, trailD.y) * pc.trailBendScale * trailI;
    vec3 bendDir = normalize(windBend + trailBend3D + vec3(1e-6));
    
    // Bend amount increases quadratically along blade (more at tip)
    float bendAmt = t * t;
    
    // Blade geometry (two-sided triangle strip)
    vec3 right = normalize(cross(up, vec3(1.0, 0.0, 0.0)));
    if (length(right) < 0.1) {
        right = normalize(cross(up, vec3(0.0, 0.0, 1.0)));
    }
    
    // Blade width (tapers toward tip)
    float width = mix(0.025, 0.005, t);
    vec3 widthOffset = right * (sideIdx == 0 ? -width : width);
    
    // Build vertex position
    vec3 pos = basePos;
    pos += up * (height * t);                    // Grow upward
    pos += bendDir * (height * bendAmt * 0.4);   // Bend
    pos += widthOffset;                          // Width offset
    
    // Transform to clip space
    gl_Position = camera.projection * camera.view * vec4(pos, 1.0);
    
    // Outputs for fragment shader
    fragNormal = up;  // Use blade normal (simple lighting)
    
    // Grass color gradient (darker at base, lighter at tip)
    vec3 colorBase = vec3(0.15, 0.35, 0.1);
    vec3 colorTip = vec3(0.4, 0.7, 0.2);
    fragColor = mix(colorBase, colorTip, t);
    
    // Ambient occlusion (darker at base)
    fragAO = mix(0.6, 1.0, t);
}

