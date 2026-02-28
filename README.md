# Minecraft Voxelizer

Convert any `.obj` / `.gltf` / `.glb` 3-D model into a Minecraft item model (`.json` + `.png`) in seconds.

---

## Features

- **Geometry-only greedy meshing** — adjacent exposed voxel faces are merged regardless of colour, minimising element count for maximum MC compatibility
- **Configurable texture density** — `--density N` maps `N×N` pixels per voxel, giving detailed colour gradients without adding extra elements
- **Multi-format input** — Wavefront OBJ (with MTL materials) and glTF 1.x / glTF 2.0 (ASCII `.gltf` and binary `.glb`)
- **Five quality levels** — 16³ to 64³ voxel grid; pick the right balance of detail vs. element count
- **Optional solid fill** — flood-fill interior voxels so hollow meshes become solid models
- **Interactive mode** — double-click the binary and answer prompts (no terminal required)

---

## Building

**Requirements:** CMake ≥ 3.20, a C++20 compiler (GCC 12+, MSVC 2022+, Clang 15+), internet access (FetchContent pulls dependencies).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary is placed in `build/bin/mc_voxelizer` (Linux/macOS) or `build\bin\Release\mc_voxelizer.exe` (Windows).

---

## Usage

```
mc_voxelizer <input> [options]
```

| Option | Default | Description |
|---|---|---|
| `--quality 1-5` | `3` | Voxel grid resolution (see table below) |
| `--density 1-16` | `1` | Texture pixels per voxel |
| `--output <dir>` | `./output` | Directory to write `.json` and `.png` |
| `--name <str>` | filename stem | Model name used in output filenames |
| `--modid <str>` | `mymod` | Mod namespace for the texture path |
| `--solid` | off | Fill interior voxels (for hollow meshes) |
| `--help` | — | Show usage |

### Quality levels

| Level | Grid | Typical elements | Notes |
|---|---|---|---|
| 1 | 16³ | < 50 | Fastest; very blocky; icon use |
| 2 | 24³ | ~100 | |
| **3** | **32³** | **~200** | **Recommended default** |
| 4 | 48³ | ~600 | Good for detailed shapes |
| 5 | 64³ | ~1 500 | Slowest; may lag Blockbench |

### Density and texture size

`--density N` controls how many pixels each voxel face occupies in the PNG atlas.  Greedy meshing is geometry-only, so element count is *unaffected* by density — only the PNG grows.

| Density | Quality 3 atlas (approx) | Use case |
|---|---|---|
| 1 | ~256 × 2 px | Flat colours, smallest file |
| 2 | ~512 × 4 px | Slight colour gradient |
| 4 | ~1 024 × 8 px | Clear colour variation / face expression |
| 8 | ~2 048 × 16 px | High detail; good for character models |

> **Warning:** density ≥ 8 with quality 5 can produce atlases larger than 8 192 px — the tool will warn you.

### Examples

```bash
# Simple sword
mc_voxelizer sword.obj

# Dragon with rich colour detail
mc_voxelizer dragon.gltf --quality 4 --density 4 --modid darkaddons --name dragon

# Character model with face expression visible
mc_voxelizer player.glb --quality 3 --density 8 --modid mymod --name player

# Hollow ship, filled solid, high quality
mc_voxelizer ship.obj --quality 5 --density 2 --solid --output ./assets/models
```

---

## Pipeline

```
.obj / .gltf / .glb
        │
        ▼  MeshLoader (ObjLoader / GltfLoader)
      Mesh  (vertices, triangles, materials)
        │
        ▼  Normalizer
      Mesh  (scaled to [0,16] MC space, floor-snapped)
        │
        ▼  Voxelizer  (SAT triangle–AABB, quality 1–5)
    VoxelGrid  (solid flags + per-voxel colours)
        │
        ▼  GreedyMesher  ← geometry-only merge
    [Quad]  (MC from/to + voxel-space metadata)
        │
        ▼  McModel::build(quads, grid, atlas, density)
        │     Phase 1: allocate atlas regions  (uCount×density, vCount×density) px each
        │     Phase 2: atlas.finalize()        (power-of-2 dimensions)
        │     Phase 3: fill pixels             (sample VoxelGrid per pixel)
        │     Phase 4: build McElements        (UV from finalised atlas)
        │
        ├──▶ <name>.json   (Minecraft model)
        └──▶ <name>.png    (texture atlas)
```

### Why geometry-only greedy meshing?

In v1.0 the mesher merged faces only when they shared the same colour.  This meant:

- Many small quads for complex coloured surfaces → high element count
- Texture atlas was 1 pixel wide per unique colour → no detail possible

In v1.1 the mesher merges **any** adjacent exposed faces on the same slice plane.  The resulting larger quads get an atlas region whose pixel dimensions are proportional to their voxel extents.  Colour is sampled per-pixel from the VoxelGrid, so full colour fidelity is preserved at whatever density you choose.

---

## Output

Two files are created:

```
output/
  <name>.json    ← Minecraft model (place in assets/<modid>/models/item/)
  <name>.png     ← Texture atlas   (place in assets/<modid>/textures/item/)
```

### Fabric mod integration (1.21 Mojang Mappings)

1. Copy `<name>.json` → `src/main/resources/assets/<modid>/models/item/<name>.json`
2. Copy `<name>.png`  → `src/main/resources/assets/<modid>/textures/item/<name>.png`
3. Register your item model in `ModelProvider` (or via JSON override in `assets/<modid>/items/<name>.json`)

```json
// assets/<modid>/items/<name>.json
{
  "model": {
    "type": "minecraft:model",
    "model": "<modid>:item/<name>"
  }
}
```

4. Open `<name>.json` in [Blockbench](https://www.blockbench.net/) to tweak display transforms (GUI rotation, third-person scale, etc.)

---

## Constraints and tips

| Concern | Advice |
|---|---|
| **Element limit** | Vanilla MC starts struggling above ~3 000 elements; Blockbench lags above ~500. Use `--quality 1-3` for editing. |
| **Texture size** | MC supports any power-of-2 PNG. Older GPU drivers may cap at 8 192 px. |
| **UV precision** | MC UVs are floats in `[0, 16]`. At very large atlas sizes (> 16 K px) sub-pixel precision may drift slightly. Keep density ≤ 8 for safety. |
| **Hollow models** | Use `--solid` for objects that look hollow after voxelisation (e.g. thin-walled meshes). |
| **Face expressions** | Increase `--density` (4–8) rather than `--quality` — you get richer texture without adding elements. |
| **Build time** | Quality 5 on a complex mesh can take several seconds. Release build is ~10× faster than Debug. |

---

## Dependencies (auto-fetched via CMake FetchContent)

| Library | Version | Purpose |
|---|---|---|
| [glm](https://github.com/g-truc/glm) | 1.0.1 | Math (vectors, matrices) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON serialisation |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | 2.0.0rc13 | OBJ parsing |
| [tinygltf](https://github.com/syoyo/tinygltf) | 2.9.3 | glTF/glb parsing |
| [stb](https://github.com/nothings/stb) | latest | PNG writing (`stb_image_write`) |

---

## Changelog

### v1.1.0
- **Greedy meshing is now geometry-only** — colour no longer affects merge decisions, producing far fewer elements for complex models
- **Added `--density` option** — controls texture pixels per voxel (1–16); default 1 preserves previous behaviour
- `TextureAtlas` redesigned from a 1-D colour strip to a 2-D strip-packing atlas with power-of-2 dimensions
- `McModel::build()` now accepts `VoxelGrid` and `density`; performs three-phase atlas fill (allocate → finalise → fill)
- Interactive mode now prompts for density

### v1.0.0
- Initial release: OBJ/glTF loading, SAT voxelisation, colour-based greedy meshing, 1-D colour-strip atlas