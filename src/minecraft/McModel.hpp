#pragma once

#include "minecraft/McElement.hpp"
#include "core/TextureAtlas.hpp"
#include "core/VoxelGrid.hpp"
#include "core/Mesh.hpp"
#include "GreedyMesher.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// McModel — assembles the complete Minecraft model from greedy mesher output,
// then serializes to JSON (model) and PNG (texture atlas).
//
// Texture baking pipeline (v1.2):
//   For each pixel (pu, pv) in a quad's atlas region:
//     1. Compute the pixel's exact 3D MC position on the voxel face.
//     2. Look up which mesh triangle was stored for the owning voxel.
//     3. Re-interpolate the triangle's UV coordinates at that 3D point.
//     4. Sample the original mesh texture at the interpolated UV.
//
//   This gives true sub-voxel color detail (face expressions, gradients,
//   markings) as long as the source texture resolution supports it.
//
//   density = 1  → 1 pixel per voxel (flat per-voxel color)
//   density = 4  → 4×4 pixels per voxel (4× sub-voxel resolution)
//   density = 16 → 16×16 pixels per voxel (matches a 16px/block texture)
// ─────────────────────────────────────────────────────────────────────────────
class McModel {
public:
    explicit McModel(std::string texturePath);

    void build(const std::vector<GreedyMesher::Quad> &quads,
               const VoxelGrid &grid,
               const Mesh &mesh,
               TextureAtlas &atlas,
               int density = 1);

    void writeJson(const std::string &outputPath) const;

    [[nodiscard]] int elementCount() const { return static_cast<int>(elements_.size()); }
    [[nodiscard]] bool isEmpty() const { return elements_.empty(); }

    void printStats() const;

private:
    std::string texturePath_;
    std::vector<McElement> elements_;

    [[nodiscard]] static McElement buildElement(const GreedyMesher::Quad &quad,
                                                const TextureAtlas::UVRect &uv);

    // Sample one atlas pixel from the original mesh texture via UV interpolation.
    [[nodiscard]] static glm::vec3 samplePixel(const GreedyMesher::Quad &q,
                                               int pu, int pv,
                                               int density,
                                               const VoxelGrid &grid,
                                               const Mesh &mesh);
};
