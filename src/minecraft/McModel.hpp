#pragma once

#include "minecraft/McElement.hpp"
#include "core/TextureAtlas.hpp"
#include "core/VoxelGrid.hpp"
#include "pipeline/GreedyMesher.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// McModel — assembles the complete Minecraft model from greedy mesher output,
// then serializes to JSON (model) and PNG (texture atlas).
//
// Texture-density pipeline (v1.1):
//   Each greedy quad covers one or more voxels.  The atlas paints
//   (uCount × density) × (vCount × density) pixels per quad, sampling
//   the VoxelGrid for per-voxel colours.
//
//   density = 1  →  1 pixel per voxel face  (compact, MC-safe)
//   density = 2  →  2×2 pixels per voxel
//   density = 4  →  4×4 pixels per voxel    (good detail)
//   density = 8  →  8×8 pixels per voxel    (high detail, larger PNG)
//
// Usage:
//   McModel model("darkaddons:item/my_model");
//   model.build(quads, grid, atlas, density);
//   model.writeJson("out.json");
// ─────────────────────────────────────────────────────────────────────────────
class McModel {
public:
    explicit McModel(const std::string &texturePath);

    // Convert greedy-meshed quads into McElements and fill the TextureAtlas.
    //   quads   — output from GreedyMesher::mesh()
    //   grid    — voxel grid (used for per-pixel colour sampling)
    //   atlas   — TextureAtlas to allocate regions into and fill
    //   density — pixels per voxel (≥ 1)
    void build(const std::vector<GreedyMesher::Quad> &quads,
               const VoxelGrid                       &grid,
               TextureAtlas                          &atlas,
               int                                    density = 1);

    // Serialize to Minecraft JSON model format.
    void writeJson(const std::string &outputPath) const;

    // ── Queries ───────────────────────────────────────────────────────────────
    int  elementCount() const { return static_cast<int>(elements_.size()); }
    bool isEmpty()      const { return elements_.empty(); }

    // Warn if element count is approaching problematic thresholds.
    void printStats() const;

private:
    std::string          texturePath_;
    std::vector<McElement> elements_;

    McElement buildElement(const GreedyMesher::Quad  &quad,
                           const TextureAtlas::UVRect &uv) const;
};