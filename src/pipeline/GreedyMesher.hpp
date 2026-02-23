#pragma once

#include "core/VoxelGrid.hpp"
#include <glm/glm.hpp>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GreedyMesher — converts a VoxelGrid into a list of optimized quads by
// merging adjacent same-colored exposed faces into larger rectangles.
//
// This is the critical optimization step: a 32³ grid has up to ~98,000
// potential faces, but greedy meshing typically reduces this to a few hundred
// for organic shapes, which is both Blockbench-safe and MC renderer-friendly.
//
// Algorithm: for each of 6 face directions, slice the grid along that axis.
// In each 2D slice, scan for maximal rectangles of same-colored exposed faces
// and emit one quad per rectangle. Faces already merged are marked consumed.
// ─────────────────────────────────────────────────────────────────────────────
class GreedyMesher {
public:
    // A merged quad in Minecraft model space [0,16]
    struct Quad {
        glm::vec3 from;    // min corner in MC units
        glm::vec3 to;      // max corner in MC units
        glm::vec3 color;   // flat color for this face
        Face      face;    // which face direction
    };

    struct Config {
        bool verbose = true;
        Config() = default;
    };

    // Config must be passed explicitly (MinGW limitation with nested struct defaults)
    explicit GreedyMesher(Config cfg);

    // Main entry: produces a list of quads from the voxel grid.
    std::vector<Quad> mesh(const VoxelGrid& grid) const;

private:
    Config cfg_;

    // Process all slices along one axis/face direction
    void meshFace(
        const VoxelGrid&   grid,
        Face               face,
        std::vector<Quad>& out) const;

    // Pack a color to uint32 for map keys (same as TextureAtlas)
    static uint32_t packColor(const glm::vec3& c);
};
