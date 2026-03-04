#pragma once

#include "core/VoxelGrid.hpp"
#include <glm/glm.hpp>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GreedyMesher v2.0 — 3-D greedy volume boxing.
//
// ARCHITECTURE CHANGE FROM v1.x:
//   v1.x was face-centric: for each of 6 face directions, sweep every layer
//   and find 2-D rectangles of exposed faces. One Quad per rectangle.
//
//   v2.0 is volume-centric: sweep the solid voxel space once and find maximal
//   3-D axis-aligned boxes. One Quad per *exposed face* of each box — but all
//   faces of the same box share the same from/to, so McModel's AABB packing
//   collapses them into a single McElement automatically.
//
// WHY THIS IS BETTER:
//   • An isolated voxel with 6 exposed faces: v1.x ≥ 6 elements, v2.0 = 1.
//   • A large solid interior region: v1.x emits elements for every exposed
//     face rectangle on the outer shell independently per face direction.
//     v2.0 absorbs the entire volume into a handful of boxes; interior faces
//     are simply never emitted (not exposed → not added to the element).
//   • The Minecraft renderer is face-driven — unlisted faces cost nothing.
//     A box with only a "north" face is as cheap to render as a full 6-face
//     box at runtime.
//
// WHAT IS UNCHANGED:
//   • Quad.sweepAxis/uAxis/vAxis/sweepLayer/uStart/vStart/uCount/vCount
//     carry identical semantics to v1.x — the per-face voxel-space info
//     used by McModel::samplePixel for texture baking. That function is
//     completely unmodified.
//   • Quad.face          — the face direction for this specific quad.
//   • Quad.from / Quad.to — NOW the full 3-D box extents in MC units
//     (not just a 1-voxel-thick face slab as in v1.x). This is what
//     enables AABB packing to group all faces of the same box.
// ─────────────────────────────────────────────────────────────────────────────

class GreedyMesher {
public:
    // ── Quad ─────────────────────────────────────────────────────────────────
    // Represents one exposed face of a 3-D greedy box.
    //
    // from / to  — full 3-D box in MC units [0,16].  All faces belonging to
    //              the same box share identical from/to (used for AABB packing).
    //
    // Texture-baking fields (identical meaning to v1.x, used by samplePixel):
    //   sweepAxis  — axis index perpendicular to this face (0=X 1=Y 2=Z)
    //   uAxis      — first  2-D axis of the face
    //   vAxis      — second 2-D axis of the face
    //   sweepLayer — outermost voxel index in the sweep direction
    //                (the layer whose triangle data is sampled)
    //   uStart, vStart — voxel-space origin of the face rectangle
    //   uCount, vCount — voxel extents of the face rectangle
    struct Quad {
        glm::vec3 from; // MC-unit min corner of the FULL 3-D box
        glm::vec3 to; // MC-unit max corner of the FULL 3-D box
        Face face; // which face direction this quad represents

        // ── Voxel-space baking info (unchanged from v1.x) ─────────────────
        int sweepAxis;
        int uAxis;
        int vAxis;
        int sweepLayer; // outermost voxel layer for this face
        int uStart, vStart; // voxel-space origin of the face
        int uCount, vCount; // voxel extents of the face
    };

    struct Config {
        bool verbose = true;

        Config() = default;
    };

    explicit GreedyMesher(Config cfg);

    // Produce one Quad per exposed face of each 3-D greedy box.
    [[nodiscard]] std::vector<Quad> mesh(const VoxelGrid &grid) const;

private:
    Config cfg_;
};
