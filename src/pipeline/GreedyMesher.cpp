#include "pipeline/GreedyMesher.hpp"
#include <iostream>
#include <vector>

GreedyMesher::GreedyMesher(Config cfg) : cfg_(cfg) {
}

std::vector<GreedyMesher::Quad> GreedyMesher::mesh(const VoxelGrid &grid) const {
    std::vector<Quad> quads;
    quads.reserve(4096);

    for (int f = 0; f < FACE_COUNT; f++)
        meshFace(grid, static_cast<Face>(f), quads);

    if (cfg_.verbose)
        std::cout << "[GreedyMesher] Quads (merged faces): " << quads.size()
                << "  (each quad = 1 MC element; less = better performance)\n";

    return quads;
}

// ─────────────────────────────────────────────────────────────────────────────
// meshFace — geometry-only greedy meshing for one face direction.
//
// Coordinate conventions per face:
//   Up/Down   (Y face): sweep along Y, 2-D grid in XZ
//   North/South (Z face): sweep along Z, 2-D grid in XY
//   West/East   (X face): sweep along X, 2-D grid in ZY
//
// The key difference from v1.0: the exposed-mask is a plain boolean grid.
// Any two adjacent exposed cells in the same slice are merged, regardless
// of their color.  Color is read from the VoxelGrid later, per-pixel,
// by the TextureAtlas.
// ─────────────────────────────────────────────────────────────────────────────
void GreedyMesher::meshFace(const VoxelGrid &grid,
                            Face face,
                            std::vector<Quad> &out) {
    // Map face → sweep axis and 2-D axes
    int sweepAxis, uAxis, vAxis;
    switch (face) {
        case Face::Down:
        case Face::Up: sweepAxis = 1;
            uAxis = 0;
            vAxis = 2;
            break;
        case Face::North:
        case Face::South: sweepAxis = 2;
            uAxis = 0;
            vAxis = 1;
            break;
        case Face::West:
        case Face::East: sweepAxis = 0;
            uAxis = 2;
            vAxis = 1;
            break;
    }

    int dims[3] = {grid.resX, grid.resY, grid.resZ};
    int sweepDim = dims[sweepAxis];
    int uDim = dims[uAxis];
    int vDim = dims[vAxis];

    // MC unit size per voxel on each axis
    float voxelSize = 16.0f / static_cast<float>(sweepDim);
    float voxelSizeU = 16.0f / static_cast<float>(uDim);
    float voxelSizeV = 16.0f / static_cast<float>(vDim);

    // Per-slice scratch buffers
    std::vector<uint8_t> consumed(uDim * vDim);
    std::vector<uint8_t> exposed(uDim * vDim); // 1 = exposed, 0 = not

    // Helper: (s, u, v) → voxel (x,y,z)
    auto toXYZ = [&](int s, int u, int v) -> glm::ivec3 {
        glm::ivec3 xyz;
        xyz[sweepAxis] = s;
        xyz[uAxis] = u;
        xyz[vAxis] = v;
        return xyz;
    };

    for (int s = 0; s < sweepDim; s++) {
        // ── Build boolean exposure mask for this slice ─────────────────────
        std::fill(consumed.begin(), consumed.end(), 0);
        std::fill(exposed.begin(), exposed.end(), 0);

        for (int v = 0; v < vDim; v++) {
            for (int u = 0; u < uDim; u++) {
                glm::ivec3 pos = toXYZ(s, u, v);
                if (grid.isFaceExposed(pos.x, pos.y, pos.z, face))
                    exposed[u + v * uDim] = 1;
            }
        }

        // ── Geometry-only greedy rectangle merging ─────────────────────────
        for (int v0 = 0; v0 < vDim; v0++) {
            for (int u0 = 0; u0 < uDim; u0++) {
                int startIdx = u0 + v0 * uDim;
                if (consumed[startIdx] || !exposed[startIdx]) continue;

                // Expand in u-direction
                int u1 = u0 + 1;
                while (u1 < uDim &&
                       !consumed[u1 + v0 * uDim] &&
                       exposed[u1 + v0 * uDim])
                    u1++;

                // Expand in v-direction: entire row [u0, u1) must be clear
                int v1 = v0 + 1;
                while (v1 < vDim) {
                    bool rowOk = true;
                    for (int u = u0; u < u1; u++) {
                        int idx = u + v1 * uDim;
                        if (consumed[idx] || !exposed[idx]) {
                            rowOk = false;
                            break;
                        }
                    }
                    if (!rowOk) break;
                    v1++;
                }
                // Merged rectangle: u ∈ [u0, u1), v ∈ [v0, v1)

                // Mark consumed
                for (int v = v0; v < v1; v++)
                    for (int u = u0; u < u1; u++)
                        consumed[u + v * uDim] = 1;

                // ── Emit Quad in MC space ──────────────────────────────────
                glm::vec3 from(0), to(0);
                from[sweepAxis] = s * voxelSize;
                to[sweepAxis] = (s + 1) * voxelSize;
                from[uAxis] = u0 * voxelSizeU;
                to[uAxis] = u1 * voxelSizeU;
                from[vAxis] = v0 * voxelSizeV;
                to[vAxis] = v1 * voxelSizeV;

                Quad q{};
                q.from = from;
                q.to = to;
                q.face = face;
                q.sweepAxis = sweepAxis;
                q.uAxis = uAxis;
                q.vAxis = vAxis;
                q.sweepLayer = s;
                q.uStart = u0;
                q.vStart = v0;
                q.uCount = u1 - u0;
                q.vCount = v1 - v0;
                out.push_back(q);
            }
        }
    }
}
