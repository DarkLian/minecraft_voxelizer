# Minecraft Voxelizer

Converts 3D model files (`.obj`, `.gltf`, `.glb`) into Minecraft item model JSON + texture atlas PNG, ready to drop into a resource pack.

---

## Showcase

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/cdd0839a-7716-4756-a700-cd7923ebf334" />
quality = 7, density = 16, originally from .gltf model

## Usage

Double click the .exe

---

## Quality levels

| Quality | Grid | Voxel size | Notes |
|---------|------|------------|-------|
| 1 | 16³ | 1.0 MC unit | Fastest, very blocky |
| 2 | 24³ | 0.67 | |
| 3 | 32³ | 0.5 | Recommended default |
| 4 | 48³ | 0.33 | |
| 5 | 64³ | 0.25 | Good detail |
| 6 | 96³ | 0.167 | 
| 7 | 128³ | 0.125 | Slowest

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

---

## Output

```
output/
  <n>.json    Minecraft item model
  <n>.png     Texture atlas
```

### Output JSON format

```json
{
  "textures": { "0": "<modid>:item/<n>" },
  "display":  { ... },
  "elements": [ ... ]
}
```

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

**Model becomes black in blockbench** This could occur if you are using high quality and density model. Try restarting blockbench and reload.
