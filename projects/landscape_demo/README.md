# Landscape Demo - Grassy Outdoor Terrain with Player Trails

Demonstration of GPU-generated grass on procedural heightmap terrain with interactive player trails.

## Features

### Terrain System
- **Procedural heightmap**: 1024m² terrain using multi-octave Perlin noise
- **High resolution**: 256×256 subdivided mesh with proper normals/tangents
- **GPU heightmap texture**: R32F format for compute shader sampling

### Grass System
- **GPU-generated instances**: Compute shader generates up to 1M grass blades
- **Density control**: Adjustable radius (32-256m) and density multiplier
- **Slope-aware**: Grass only spawns on reasonable slopes
- **Wind animation**: Procedural wind with per-blade variation
- **Procedural geometry**: No vertex buffer; blades generated in vertex shader

### Player Trail System
- **Flatten + bend**: Trails both flatten grass and bend it directionally
- **Programmable relaxation**: 5-20 seconds (default 12s)
- **Clipmap implementation**: 512×512 ping-pong textures, 256m coverage
- **Smooth transitions**: Exponential decay with event stamping

## Controls

### Movement
- **WASD**: Move forward/backward/left/right (creates trails!)
- **Space/C**: Move up/down
- **Mouse**: Look around
- **Shift**: Sprint (3x speed)
- **Ctrl**: Slow walk (0.25x speed)

### Grass Controls
- **G**: Toggle grass rendering on/off
- **[ / ]**: Decrease/increase grass radius
- **- / =**: Decrease/increase grass density

### Trail Controls
- **, / .**: Decrease/increase trail relax time (5-20s)
- **T**: Toggle trail debug view (planned)

### Other
- **F**: Print status (FPS, position, grass stats)
- **Tab**: Toggle mouse capture
- **Esc**: Exit

## Implementation Details

### GPU Pipeline
1. **Trail Update** (`trail_update.comp`)
   - Relaxes intensity/direction over time
   - Stamps player movement events
   - Scrolls clipmap with player

2. **Grass Generation** (`grass_generate.comp`)
   - Samples heightmap + trail field
   - Computes normals from heightmap
   - Atomically allocates instances
   - Writes indirect draw command

3. **Grass Rendering** (`grass.vert` + `grass.frag`)
   - Procedural blade expansion (12 verts/blade)
   - Wind + trail bending
   - Simple Lambert lighting

### Memory Allocation
- **Zero per-frame allocations**: All buffers pre-allocated at init
- **GPU-only work**: Compute shaders do all heavy lifting
- **Lock-free**: No mutexes or synchronization on CPU

### Buffer Sizes
- Instance buffer: ~48 MB (1M instances × 48 bytes)
- Trail textures: ~1 MB (4× 512×512 textures)
- Events SSBO: 1 KB (32 events × 32 bytes)

## Building

From project root:
```bash
cmake -B build -S .
cmake --build build --target landscape_demo
./build/bin/landscape_demo
```

## Performance

Target: 60+ FPS on mid-range GPU (tested on Apple M3 Pro)

Configurable parameters:
- Grass radius: 32-256m
- Density: 0.1-3.0x
- Cell size: 0.5m (fixed)
- Max instances: 1M (compile-time)

## Future Extensions

- LOD system (reduce segments at distance)
- Multi-species grass (colors, shapes)
- Wind zones (texture-based wind)
- GPU occlusion culling
- Trail history visualization
- Grass physics interaction

