# Minecraft Voxelizer

Converts 3D model files (`.obj`, `.gltf`, `.glb`) into Minecraft item model JSON + texture atlas PNG.

Supports sub-voxel texture baking so face expressions, gradients, and fine surface detail are preserved at high quality settings. Handles layered geometry (hair over face, eyes on face skin) correctly by storing one winning triangle per face direction per voxel.

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
| `--solid` | off | Fill enclosed interior voxels |
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
| 6 | 96³ | 0.167 | Face expressions visible |
| 7 | 128³ | 0.125 | Maximum detail (slow, ~1–3 min) |

Higher quality = finer voxels = more surface detail captured. The greedy mesher keeps element counts manageable even at quality 7 because it merges adjacent same-facing voxels into single rectangles regardless of colour.

---

## Texture density

Density controls how many pixels each voxel face gets in the output atlas. The sweet spot is:

```
optimal_density = source_texture_size / grid_resolution
```

Going above this upscales bilinearly — no extra detail is gained, and the PNG gets larger.

| Source texture | Q3 (32) | Q4 (48) | Q5 (64) | Q6 (96) | Q7 (128) |
|----------------|:-------:|:-------:|:-------:|:-------:|:--------:|
| 256 × 256 | 8 | 5 | 4 | 2 | 2 |
| 512 × 512 | 16 | 10 | 8 | 5 | 4 |
| 1024 × 1024 | 32 | 21 | 16 | 8 | 8 |
| 2048 × 2048 | 32 | 32 | 32 | 16 | 16 |

Leave `--density` unset to have the tool calculate it automatically from the loaded texture.

---

## Recommended settings for character models

```bash
# Good balance — 2048px source texture
mc_voxelizer character.glb --quality 6 --modid mymod --name character

# Maximum detail
mc_voxelizer character.glb --quality 7 --modid mymod --name character

# Solid interior fill (useful for chunky props, not characters)
mc_voxelizer prop.glb --quality 4 --solid --modid mymod --name prop
```

---

## Output

```
output/
  <name>.json    Minecraft item model
  <name>.png     Texture atlas
```

Copy to your resource pack:
```
assets/<modid>/models/item/<name>.json
assets/<modid>/textures/item/<name>.png
```

Reference in your item definition (Fabric / 1.21+ data-driven items):
```json
{ "model": { "type": "minecraft:model", "model": "<modid>:item/<name>" } }
```

### Output JSON format

```json
{
  "textures": { "0": "<modid>:item/<name>" },
  "elements": [ ... ],
  "display":  { ... }
}
```

- No `parent` field — the model is standalone
- No `particle` texture — not needed for item models
- `textures` appears at the top of the file

---

## How it works

1. **Load** — OBJ or glTF/GLB loaded with all materials and textures
2. **Normalize** — mesh scaled uniformly to fit MC's `[0, 16]` coordinate space, optionally snapped to floor
3. **Voxelize** — surface voxelization via SAT triangle–AABB overlap tests. Each voxel stores up to 6 triangle indices (one per face direction). A triangle only competes for face slots whose outward normal aligns with the triangle's normal (`dot > 0.1`). Within each slot, the closest triangle wins. This correctly resolves layered geometry: an eye-area voxel stores the face-skin triangle for its South slot and the hair triangle for its Up slot
4. **Greedy mesh** — adjacent exposed voxel faces merged into maximal rectangles (geometry-only, colour-independent). Fewer elements = better MC performance
5. **Bake** — for each pixel in the atlas, the owning voxel's stored triangle is looked up for the quad's face direction, barycentric coordinates are computed at the pixel's exact 3D position, and the original mesh texture is sampled at the interpolated UV. Falls back to any recorded triangle, then to the flat per-voxel colour
6. **Write** — atlas PNG and model JSON written to output directory

---

## Texture atlas

Atlas dimensions use independent power-of-2 per axis (non-square). The packer estimates a square-ish row width from `sqrt(totalPixels)` before allocating, keeping both axes similar in size. Non-square power-of-2 textures (e.g. `4096×2048`) are fully supported by Minecraft 1.13+.

Approximate atlas sizes at common settings:

| Quality | Density | Approx. atlas |
|---------|---------|---------------|
| 5 | 8 | 2048 × 2048 |
| 6 | 8 | 4096 × 2048 |
| 7 | 8 | 4096 × 4096 |
| 7 | 16 | 8192 × 4096 |

---

## Troubleshooting

**Eyes / face details missing**
Use quality 6 or 7. At quality 5 the face region may only be ~10 voxels tall — not enough to resolve fine features like anime eyes.

**PNG is very large or slow to generate**
Reduce `--density`. The sweet spot is `texture_size / grid_resolution`; anything above that adds no detail and increases file size.

**Black voxels**
Check the loader output for texture warnings. For OBJ, ensure the `.mtl` file and texture images are in the same directory as the `.obj`. For glTF, embedded textures (`.glb`) always work; external ones need to be alongside the `.gltf`.

**Model loaded but renders purple/black in-game**
The texture PNG is missing or in the wrong location. Verify `assets/<modid>/textures/item/<name>.png` exists in the pack.

**Resource pack fails to load intermittently**
MC caches compiled model data. Remove and re-add the resource pack, or delete `.minecraft/assets/` to force a fresh build.