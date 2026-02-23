#include "pipeline/GreedyMesher.hpp"
#include <cmath>
#include <iostream>
#include <vector>

GreedyMesher::GreedyMesher(Config cfg) : cfg_(cfg) {}

uint32_t GreedyMesher::packColor(const glm::vec3& c) {
    uint8_t r = static_cast<uint8_t>(std::round(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    uint8_t g = static_cast<uint8_t>(std::round(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    uint8_t b = static_cast<uint8_t>(std::round(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

std::vector<GreedyMesher::Quad> GreedyMesher::mesh(const VoxelGrid& grid) const {
    std::vector<Quad> quads;
    quads.reserve(4096);

    for (int f = 0; f < FACE_COUNT; f++)
        meshFace(grid, static_cast<Face>(f), quads);

    if (cfg_.verbose)
        std::cout << "[GreedyMesher] Generated " << quads.size() << " quads.\n";

    return quads;
}

// ─────────────────────────────────────────────────────────────────────────────
// meshFace — greedy mesh all slices for one face direction.
//
// Coordinate conventions per face:
//   Up/Down   (Y face): sweep along Y, 2D grid in XZ
//   North/South (Z face): sweep along Z, 2D grid in XY
//   West/East   (X face): sweep along X, 2D grid in ZY
// ─────────────────────────────────────────────────────────────────────────────
void GreedyMesher::meshFace(
    const VoxelGrid& grid,
    Face face,
    std::vector<Quad>& out) const
{
    // Map face → sweep axis and 2D axes
    // sweepAxis: the axis we iterate slices along
    // u, v: the two axes forming the 2D slice grid
    int sweepAxis, uAxis, vAxis;
    switch (face) {
        case Face::Down:
        case Face::Up:    sweepAxis = 1; uAxis = 0; vAxis = 2; break;
        case Face::North:
        case Face::South: sweepAxis = 2; uAxis = 0; vAxis = 1; break;
        case Face::West:
        case Face::East:  sweepAxis = 0; uAxis = 2; vAxis = 1; break;
    }

    // Grid dimensions along each logical axis
    int dims[3] = { grid.resX, grid.resY, grid.resZ };
    int sweepDim = dims[sweepAxis];
    int uDim     = dims[uAxis];
    int vDim     = dims[vAxis];

    // MC unit size per voxel
    float voxelSize = 16.0f / static_cast<float>(sweepDim);
    // (same for all axes since we use a cubic grid)
    float voxelSizeU = 16.0f / static_cast<float>(uDim);
    float voxelSizeV = 16.0f / static_cast<float>(vDim);

    // Consumed mask for the current 2D slice
    std::vector<uint8_t>  consumed(uDim * vDim);
    // Color map for the current 2D slice (0 = empty)
    std::vector<uint32_t> colorGrid(uDim * vDim);

    // Helper: convert (sweepIdx, u, v) → voxel (x,y,z)
    auto toXYZ = [&](int s, int u, int v) -> glm::ivec3 {
        glm::ivec3 xyz;
        xyz[sweepAxis] = s;
        xyz[uAxis]     = u;
        xyz[vAxis]     = v;
        return xyz;
    };

    for (int s = 0; s < sweepDim; s++) {
        // ── Build 2D face-exposure mask for this slice ─────────────────────
        std::fill(consumed.begin(),   consumed.end(),   0);
        std::fill(colorGrid.begin(), colorGrid.end(),  0);

        for (int v = 0; v < vDim; v++) {
            for (int u = 0; u < uDim; u++) {
                glm::ivec3 pos = toXYZ(s, u, v);
                if (grid.isFaceExposed(pos.x, pos.y, pos.z, face)) {
                    glm::vec3 color = grid.getColor(pos.x, pos.y, pos.z);
                    colorGrid[u + v * uDim] = packColor(color);
                } else {
                    colorGrid[u + v * uDim] = 0; // not exposed or empty
                }
            }
        }

        // ── Greedy rectangle merging ────────────────────────────────────────
        for (int v0 = 0; v0 < vDim; v0++) {
            for (int u0 = 0; u0 < uDim; u0++) {
                int startIdx = u0 + v0 * uDim;
                if (consumed[startIdx]) continue;
                uint32_t targetColor = colorGrid[startIdx];
                if (targetColor == 0) continue; // empty

                // Expand in u direction first
                int u1 = u0 + 1;
                while (u1 < uDim &&
                       !consumed[u1 + v0 * uDim] &&
                       colorGrid[u1 + v0 * uDim] == targetColor)
                {
                    u1++;
                }
                // u0..u1-1 is the width of the rectangle

                // Now expand in v direction: check entire row [u0, u1) at each v
                int v1 = v0 + 1;
                while (v1 < vDim) {
                    bool rowOk = true;
                    for (int u = u0; u < u1; u++) {
                        int idx = u + v1 * uDim;
                        if (consumed[idx] || colorGrid[idx] != targetColor) {
                            rowOk = false;
                            break;
                        }
                    }
                    if (!rowOk) break;
                    v1++;
                }
                // Rectangle: u in [u0, u1), v in [v0, v1)

                // Mark consumed
                for (int v = v0; v < v1; v++)
                    for (int u = u0; u < u1; u++)
                        consumed[u + v * uDim] = 1;

                // ── Emit Quad in MC space ──────────────────────────────────
                // Reconstruct float MC coordinates from voxel indices
                glm::vec3 from(0), to(0);

                // Sweep dimension: both from and to depend on face direction
                // For a +face (Up, South, East): quad sits at outer face
                // For a -face (Down, North, West): quad sits at inner face
                float sCoordInner = s       * voxelSize;
                float sCoordOuter = (s + 1) * voxelSize;

                from[sweepAxis] = sCoordInner;
                to[sweepAxis]   = sCoordOuter;

                from[uAxis] = u0 * voxelSizeU;
                to[uAxis]   = u1 * voxelSizeU;
                from[vAxis] = v0 * voxelSizeV;
                to[vAxis]   = v1 * voxelSizeV;

                // Recover color from packed uint32
                uint8_t cr = (targetColor >> 16) & 0xFF;
                uint8_t cg = (targetColor >>  8) & 0xFF;
                uint8_t cb =  targetColor         & 0xFF;
                glm::vec3 color(cr / 255.0f, cg / 255.0f, cb / 255.0f);

                out.push_back({ from, to, color, face });
            }
        }
    }
}
