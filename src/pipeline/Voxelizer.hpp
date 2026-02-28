#pragma once

#include "core/Mesh.hpp"
#include "core/VoxelGrid.hpp"
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Voxelizer — converts a normalized Mesh into a VoxelGrid.
//
// Quality → resolution (voxels per 16 MC units):
//   1 →  16³   fastest, very blocky
//   2 →  24³
//   3 →  32³   recommended default
//   4 →  48³
//   5 →  64³   good detail
//   6 →  96³   face expressions visible
//   7 → 128³   maximum detail (slow, ~1-3 min for complex meshes)
// ─────────────────────────────────────────────────────────────────────────────
class Voxelizer {
public:
    struct Config {
        int  quality   = 3;
        bool solidFill = false;
        bool verbose   = true;
        Config() = default;
    };

    static int qualityToResolution(int quality);

    explicit Voxelizer(Config cfg);

    VoxelGrid voxelize(const Mesh &mesh) const;

private:
    Config cfg_;

    bool triangleOverlapsVoxel(
        const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2,
        const glm::vec3 &voxelCenter,
        const glm::vec3 &halfSize) const;

    glm::vec3 barycentricCoords(
        const glm::vec3 &p,
        const glm::vec3 &a,
        const glm::vec3 &b,
        const glm::vec3 &c) const;

    int floodFillSolid(VoxelGrid &grid) const;
};