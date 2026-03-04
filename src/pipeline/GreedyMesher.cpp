#include "pipeline/GreedyMesher.hpp"
#include <iostream>
#include <vector>
#include <algorithm>

GreedyMesher::GreedyMesher(Config cfg) : cfg_(cfg) {
}

std::vector<GreedyMesher::Quad> GreedyMesher::mesh(const VoxelGrid &grid) const {
    std::vector<Quad> quads;
    quads.reserve(4096);

    for (int f = 0; f < FACE_COUNT; f++)
        meshFace(grid, static_cast<Face>(f), quads);

    if (cfg_.verbose)
        std::cout << "[GreedyMesher] Quads (merged faces): " << quads.size()
                << "  (each quad = 1 MC element face; less = better performance)\n";

    return quads;
}

// ─────────────────────────────────────────────────────────────────────────────
// meshFace — geometry-only greedy meshing for one face direction.
//
// OPTIMIZATION v1.3: Dual-pass greedy rectangle decomposition.
//   Standard greedy meshing commits to expanding u first, then v. This is
//   scan-order-dependent and produces a suboptimal rectangle count for many
//   shapes. We now run the greedy algorithm TWICE per slice:
//     Pass A — expand u-direction first, then v (original behavior)
//     Pass B — expand v-direction first, then u (transposed)
//   We keep whichever pass produces fewer rectangles for that slice.
//
//   Cost: 2× the merge work per slice, which is negligible vs. voxelization.
//   Gain: measurably fewer quads for cross-shaped and L-shaped voxel regions.
//
// Coordinate conventions per face:
//   Up/Down   (Y face): sweep along Y, 2-D grid in XZ
//   North/South (Z face): sweep along Z, 2-D grid in XY
//   West/East   (X face): sweep along X, 2-D grid in ZY
// ─────────────────────────────────────────────────────────────────────────────
void GreedyMesher::meshFace(const VoxelGrid &grid,
                            Face face,
                            std::vector<Quad> &out) {
    // Map face → sweep axis and 2-D axes
    int sweepAxis, uAxis, vAxis;
    switch (face) {
        case Face::Down:
        case Face::Up:    sweepAxis = 1; uAxis = 0; vAxis = 2; break;
        case Face::North:
        case Face::South: sweepAxis = 2; uAxis = 0; vAxis = 1; break;
        case Face::West:
        case Face::East:  sweepAxis = 0; uAxis = 2; vAxis = 1; break;
    }

    int dims[3] = {grid.resX, grid.resY, grid.resZ};
    int sweepDim = dims[sweepAxis];
    int uDim     = dims[uAxis];
    int vDim     = dims[vAxis];

    float voxelSize  = 16.0f / static_cast<float>(sweepDim);
    float voxelSizeU = 16.0f / static_cast<float>(uDim);
    float voxelSizeV = 16.0f / static_cast<float>(vDim);

    // Scratch buffers reused across slices
    std::vector<uint8_t> exposed(uDim * vDim);
    std::vector<uint8_t> consumedA(uDim * vDim);
    std::vector<uint8_t> consumedB(uDim * vDim);

    // Helper: (s, u, v) → voxel (x,y,z)
    auto toXYZ = [&](int s, int u, int v) -> glm::ivec3 {
        glm::ivec3 xyz;
        xyz[sweepAxis] = s;
        xyz[uAxis]     = u;
        xyz[vAxis]     = v;
        return xyz;
    };

    // Tiny POD rect used during the two passes (no Quad heap overhead)
    struct Rect { int u0, v0, u1, v1; };

    for (int s = 0; s < sweepDim; s++) {
        // ── Build boolean exposure mask for this slice ─────────────────────
        for (int v = 0; v < vDim; v++) {
            for (int u = 0; u < uDim; u++) {
                glm::ivec3 pos = toXYZ(s, u, v);
                exposed[u + v * uDim] =
                    grid.isFaceExposed(pos.x, pos.y, pos.z, face) ? 1 : 0;
            }
        }

        // ── Pass A: expand u first, then v ────────────────────────────────
        std::vector<Rect> rectsA;
        std::ranges::fill(consumedA, 0);

        for (int v0 = 0; v0 < vDim; v0++) {
            for (int u0 = 0; u0 < uDim; u0++) {
                if (consumedA[u0 + v0 * uDim] || !exposed[u0 + v0 * uDim])
                    continue;

                // Expand in u
                int u1 = u0 + 1;
                while (u1 < uDim &&
                       !consumedA[u1 + v0 * uDim] &&
                       exposed[u1 + v0 * uDim])
                    u1++;

                // Expand in v (entire [u0, u1) row must be clear)
                int v1 = v0 + 1;
                while (v1 < vDim) {
                    bool rowOk = true;
                    for (int u = u0; u < u1; u++) {
                        if (consumedA[u + v1 * uDim] || !exposed[u + v1 * uDim]) {
                            rowOk = false; break;
                        }
                    }
                    if (!rowOk) break;
                    v1++;
                }

                for (int v = v0; v < v1; v++)
                    for (int u = u0; u < u1; u++)
                        consumedA[u + v * uDim] = 1;

                rectsA.push_back({u0, v0, u1, v1});
            }
        }

        // ── Pass B: expand v first, then u ────────────────────────────────
        std::vector<Rect> rectsB;
        std::ranges::fill(consumedB, 0);

        for (int v0 = 0; v0 < vDim; v0++) {
            for (int u0 = 0; u0 < uDim; u0++) {
                if (consumedB[u0 + v0 * uDim] || !exposed[u0 + v0 * uDim])
                    continue;

                // Expand in v first
                int v1 = v0 + 1;
                while (v1 < vDim &&
                       !consumedB[u0 + v1 * uDim] &&
                       exposed[u0 + v1 * uDim])
                    v1++;

                // Expand in u (entire [v0, v1) column must be clear)
                int u1 = u0 + 1;
                while (u1 < uDim) {
                    bool colOk = true;
                    for (int v = v0; v < v1; v++) {
                        if (consumedB[u1 + v * uDim] || !exposed[u1 + v * uDim]) {
                            colOk = false; break;
                        }
                    }
                    if (!colOk) break;
                    u1++;
                }

                for (int v = v0; v < v1; v++)
                    for (int u = u0; u < u1; u++)
                        consumedB[u + v * uDim] = 1;

                rectsB.push_back({u0, v0, u1, v1});
            }
        }

        // ── Choose the pass that yielded fewer rectangles ──────────────────
        const std::vector<Rect> &best =
            (rectsA.size() <= rectsB.size()) ? rectsA : rectsB;

        // ── Emit Quads in MC space ─────────────────────────────────────────
        for (const auto &r : best) {
            glm::vec3 from(0), to(0);
            from[sweepAxis] = s * voxelSize;
            to[sweepAxis]   = (s + 1) * voxelSize;
            from[uAxis]     = r.u0 * voxelSizeU;
            to[uAxis]       = r.u1 * voxelSizeU;
            from[vAxis]     = r.v0 * voxelSizeV;
            to[vAxis]       = r.v1 * voxelSizeV;

            Quad q{};
            q.from       = from;
            q.to         = to;
            q.face       = face;
            q.sweepAxis  = sweepAxis;
            q.uAxis      = uAxis;
            q.vAxis      = vAxis;
            q.sweepLayer = s;
            q.uStart     = r.u0;
            q.vStart     = r.v0;
            q.uCount     = r.u1 - r.u0;
            q.vCount     = r.v1 - r.v0;
            out.push_back(q);
        }
    }
}
