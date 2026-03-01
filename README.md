# Minecraft Voxelizer

Converts 3D model files (`.obj`, `.gltf`, `.glb`) into Minecraft item model JSON + texture atlas PNG, ready to drop into a resource pack.

## Building

Requires CMake 3.20+ and a C++20 compiler. All dependencies are fetched automatically via CMake FetchContent (GLM, nlohmann/json, tinyobjloader, tinygltf, stb).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/bin/mc_voxelizer.exe` (Windows) or `build/bin/mc_voxelizer` (Linux/macOS).

Static linking is enabled on Windows so the binary runs without MinGW DLLs.

---

## Usage

### Interactive mode

Run with no arguments (or double-click the `.exe`) to be prompted for each setting:

```
mc_voxelizer
```

### Command line

```
mc_voxelizer <input.obj|.gltf|.glb> [options]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--quality 1-7` | `3` | Voxel grid resolution |
| `--density 1-64` | auto | Texture pixels per voxel face |
| `--output <dir>` | `./output` | Output directory |
| `--name <str>` | filename stem | Model and file name |
| `--modid <str>` | `mymod` | Mod namespace for the texture path |
| `--solid` | on | Fill enclosed interior voxels |
| `--help` | | Print usage |

---

## Quality levels

| Quality | Grid | Voxel size | Notes |
|---------|------|------------|-------|
| 1 | 16³ | 1.0 MC unit | Fastest, very blocky |
| 2 | 24³ | 0.67 | |
| 3 | 32³ | 0.5 | Recommended default |
| 4 | 48³ | 0.33 | |
| 5 | 64³ | 0.25 | Good detail |
| 6 | 96³ | 0.167 | Fine surface detail |
| 7 | 128³ | 0.125 | Maximum detail (slow, ~1–3 min) |

Higher quality = finer voxels = more surface detail. The greedy mesher keeps element counts manageable even at quality 7 by merging adjacent same-facing voxels into single rectangles.

---

## Texture density

Density controls how many pixels each voxel face gets in the output atlas. The sweet spot is:

```
optimal_density = source_texture_size / grid_resolution
```

Going above this upscales bilinearly — no extra detail is gained and the PNG gets larger.

| Source texture | Q3 (32) | Q4 (48) | Q5 (64) | Q6 (96) | Q7 (128) |
|----------------|:-------:|:-------:|:-------:|:-------:|:--------:|
| 256 × 256 | 8 | 5 | 4 | 2 | 2 |
| 512 × 512 | 16 | 10 | 8 | 5 | 4 |
| 1024 × 1024 | 32 | 21 | 16 | 8 | 8 |
| 2048 × 2048 | 32 | 32 | 32 | 16 | 16 |

Leave `--density` unset to have the tool calculate it automatically from the loaded texture.

---

## Recommended settings

```bash
# Solid prop or weapon
mc_voxelizer sword.glb --quality 4 --modid mymod --name sword

# Character body (no fine facial detail)
mc_voxelizer character.glb --quality 6 --modid mymod --name character

# Maximum geometric detail
mc_voxelizer character.glb --quality 7 --modid mymod --name character
```

---

## Output

```
output/
  <n>.json    Minecraft item model
  <n>.png     Texture atlas
```

Copy to your resource pack:
```
assets/<modid>/models/item/<n>.json
assets/<modid>/textures/item/<n>.png
```

Reference in your item definition (Fabric / 1.21+ data-driven items):
```json
{ "model": { "type": "minecraft:model", "model": "<modid>:item/<n>" } }
```

### Output JSON format

```json
{
  "textures": { "0": "<modid>:item/<n>" },
  "display":  { ... },
  "elements": [ ... ]
}
```

- No `parent` field — the model is standalone
- No `particle` texture
- Keys are ordered: `textures` → `display` → `elements`, so display settings are easy to find without scrolling past thousands of element entries
- Primitive arrays (coordinates, UVs) are written inline to keep file size small

---

## Limitations

**Fine facial detail** (eyes, eyelashes, skin gradients) does not survive voxelization well. Even at quality 7 (128³), a face region is only ~20–30 voxels tall — not enough resolution to represent features that span just a few pixels of the source texture. If facial detail matters, use the voxelizer for the body/hair geometry and hand-paint the face area in Blockbench afterwards using the generated model as a base.

**Thin geometry** (hair strands, fabric edges, accessories) may voxelize inconsistently depending on orientation relative to the grid. Inspect the result in Blockbench and clean up as needed.

---

## How it works

1. **Load** — OBJ or glTF/GLB loaded with all materials and textures
2. **Normalize** — mesh scaled uniformly to fit MC's `[0, 16]` coordinate space, snapped to floor
3. **Voxelize** — surface voxelization via SAT triangle–AABB overlap tests. Each voxel stores one triangle index per face direction (6 slots). Within each slot the closest triangle wins by distance to its plane
4. **Greedy mesh** — adjacent exposed voxel faces merged into maximal rectangles (geometry-only). Fewer elements = better MC performance
5. **Bake** — for each atlas pixel, the owning voxel's stored triangle is looked up, barycentric coordinates computed at the pixel's exact 3D position, and the original mesh texture sampled at the interpolated UV
6. **Write** — atlas PNG and compact model JSON written to output directory

## Texture atlas

Atlas dimensions use independent power-of-2 per axis (non-square). The packer estimates a square-ish row width from `sqrt(totalPixels)` to keep both axes balanced. Non-square power-of-2 textures (e.g. `4096×2048`) are fully supported by Minecraft 1.13+.

Approximate atlas sizes at common settings:

| Quality | Density | Approx. atlas |
|---------|---------|---------------|
| 5 | 8 | 2048 × 2048 |
| 6 | 8 | 4096 × 2048 |
| 7 | 8 | 4096 × 4096 |
| 7 | 16 | 8192 × 4096 |

---

## Troubleshooting

**Black voxels**
Check the loader output for texture warnings. For OBJ, ensure the `.mtl` file and textures are in the same directory. For glTF, embedded (`.glb`) always works; external textures must be alongside the `.gltf`.

**PNG is very large or slow to generate**
Reduce `--density`. The sweet spot is `texture_size / grid_resolution` — anything above adds no detail.

**Model renders purple/black in-game**
The texture PNG is missing or in the wrong path. Verify `assets/<modid>/textures/item/<n>.png` exists in the pack.

**Resource pack fails to reload intermittently**
MC caches compiled model data. Remove and re-add the resource pack, or delete `.minecraft/assets/` to force a fresh build.
