#pragma once

#include "core/VoxelGrid.hpp"
#include <glm/glm.hpp>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GreedyMesher — converts a VoxelGrid into a list of optimised quads by
// merging adjacent *exposed* faces into larger rectangles.
//
// Key design change (v1.1):
//   Merging is now purely geometry-based — adjacent exposed faces are merged
//   regardless of colour.  Per-voxel colour is painted into the TextureAtlas
//   at a configurable pixel density (see McModel::build / --density flag).
//
// Benefits:
//   • Dramatically fewer quads for organic shapes (element-limit friendly)
//   • Enables sub-voxel texture detail via the density parameter
//   • Clean separation: geometry ↔ appearance
//
// Algorithm: for each of 6 face directions, slice the grid along that axis.
// In each 2D slice, scan for maximal rectangles of exposed faces (colour-
// agnostic) and emit one Quad per rectangle.  Already-consumed faces are
// skipped.
// ─────────────────────────────────────────────────────────────────────────────
class GreedyMesher {
public:
    // A merged quad in Minecraft model space [0,16].
    // Also carries enough voxel-space info for the TextureAtlas to sample
    // per-voxel colours at arbitrary pixel density.
    struct Quad {
        glm::vec3 from;      // MC-unit min corner
        glm::vec3 to;        // MC-unit max corner
        Face      face;      // which face direction

        // ── Voxel-space info (used by TextureAtlas pixel fill) ────────────
        int sweepAxis;       // axis index the slice runs along (0/1/2)
        int uAxis;           // first  2-D axis index
        int vAxis;           // second 2-D axis index
        int sweepLayer;      // s-index of this slice (voxel coordinate)
        int uStart, vStart;  // voxel index of rect origin
        int uCount, vCount;  // voxel extents of rect
    };

    struct Config {
        bool verbose = true;
        Config() = default;
    };

    explicit GreedyMesher(Config cfg);

    // Produce a list of geometry-merged quads from the voxel grid.
    std::vector<Quad> mesh(const VoxelGrid &grid) const;

private:
    Config cfg_;

    void meshFace(const VoxelGrid &grid,
                  Face             face,
                  std::vector<Quad> &out) const;
};