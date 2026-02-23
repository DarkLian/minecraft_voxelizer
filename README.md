# Minecraft Voxelizer

A C++ tool that converts `.obj` and `.gltf`/`.glb` 3D models into Minecraft-compatible
model JSON files with a generated texture atlas PNG.

## Pipeline

```
.obj / .gltf
     │
     ▼
[MeshLoader]       Load geometry + materials
     │
     ▼
[Normalizer]       Scale mesh to Minecraft [0,16] coordinate space
     │
     ▼
[Voxelizer]        Surface voxelization via SAT triangle–AABB test
     │
     ▼
[GreedyMesher]     Merge adjacent same-color faces → optimized quads
     │
     ▼
[TextureAtlas]     Deduplicate colors → compact PNG strip
     │
     ▼
[McModel]          Assemble + write model.json + texture.png
```

## Building

### Prerequisites
- CMake 3.20+
- C++20 compiler (GCC 11+, Clang 12+, MSVC 2022+)
- Git (for FetchContent dependencies)

### Steps

```bash
# 1. Get stb (header-only, needed for PNG writing)
mkdir -p third_party/stb
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
mv stb_image*.h third_party/stb/

# 2. Configure + build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Binary will be at: build/bin/mc_voxelizer
```

## Usage

```bash
mc_voxelizer <input.obj|.gltf|.glb> [options]

Options:
  --quality  1-5    Voxel resolution (default: 3)
                      1 = 16³  fastest, most blocky
                      2 = 24³
                      3 = 32³  recommended default
                      4 = 48³
                      5 = 64³  finest detail, most elements
  --output   <dir>  Output directory (default: ./output)
  --name     <str>  Model/file name stem (default: input filename)
  --modid    <str>  Mod namespace for texture path (default: mymod)
  --solid           Fill interior with voxels (slower, better for solid objects)
  --help            Show usage
```

### Examples

```bash
# Basic conversion (quality 3 default)
mc_voxelizer sword.obj

# High-quality dragon model for the "darkaddons" mod
mc_voxelizer dragon.gltf --quality 4 --modid darkaddons --name dragon

# Solid-fill ship model at max quality
mc_voxelizer ship.obj --quality 5 --solid --output ./my_assets

# Quick low-quality preview
mc_voxelizer model.obj --quality 1
```

## Output

Running the tool produces two files:

| File | Description |
|---|---|
| `<name>.json` | Minecraft model file — drop into `assets/<modid>/models/item/` |
| `<name>.png`  | Texture atlas — drop into `assets/<modid>/textures/item/` |

## Quality Guide

| Level | Resolution | Typical Elements After Greedy | Use Case |
|---|---|---|---|
| 1 | 16³   | ~50–150   | Icons, simple items |
| 2 | 24³   | ~100–300  | Medium detail items |
| 3 | 32³   | ~200–600  | Default, good balance |
| 4 | 48³   | ~400–1200 | Fine detail, complex shapes |
| 5 | 64³   | ~800–2500 | Max detail (edit in Blockbench after) |

**Note:** Blockbench starts lagging around 500 elements. Vanilla Minecraft
renderer becomes unstable past ~3000. If quality 5 exceeds these limits,
reduce to quality 3–4 and post-process in Blockbench.

## Importing into Blockbench

1. Open Blockbench → **File → Open Model**
2. Select your generated `.json` file
3. Adjust **Display** settings (GUI, third-person, etc.)
4. Export back to JSON when done

## Project Structure

```
src/
├── main.cpp                  # CLI entry point + pipeline orchestration
├── core/
│   ├── Mesh.hpp/.cpp         # Format-agnostic geometry container
│   ├── VoxelGrid.hpp/.cpp    # 3D voxel grid with face exposure queries
│   └── TextureAtlas.hpp/.cpp # Color dedup + PNG atlas writer
├── io/
│   ├── MeshLoader.hpp/.cpp   # Abstract loader + factory method
│   ├── ObjLoader.hpp/.cpp    # Wavefront OBJ via tinyobjloader
│   └── GltfLoader.hpp/.cpp   # GLTF/GLB via tinygltf
├── pipeline/
│   ├── Normalizer.hpp/.cpp   # Scale mesh to MC [0,16] space
│   ├── Voxelizer.hpp/.cpp    # SAT surface voxelization
│   └── GreedyMesher.hpp/.cpp # Adjacent-face merging optimization
└── minecraft/
    ├── McConstants.hpp       # MC format constants, face names
    ├── McElement.hpp         # JSON element data structure
    └── McModel.hpp/.cpp      # Assembles + serializes complete model

third_party/
├── stb/                      # stb_image_write (PNG output)
└── (glm, nlohmann, tinyobjloader, tinygltf via CMake FetchContent)
```

## Dependencies

All fetched automatically via CMake FetchContent except `stb`:

| Library | Purpose |
|---|---|
| [glm](https://github.com/g-truc/glm) | 3D math (vectors, matrices) |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON serialization |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | OBJ loading |
| [tinygltf](https://github.com/syoyo/tinygltf) | GLTF/GLB loading |
| [stb](https://github.com/nothings/stb) | PNG writing (manual download) |
