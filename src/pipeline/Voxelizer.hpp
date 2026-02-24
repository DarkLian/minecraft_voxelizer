#pragma once

#include "core/Mesh.hpp"
#include "core/VoxelGrid.hpp"
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Voxelizer — converts a normalized Mesh into a VoxelGrid.
//
// Quality levels map to voxel resolution (voxels per 16 MC units):
//   Quality 1 →  16³  (fastest, blockiest — good for icons)
//   Quality 2 →  24³
//   Quality 3 →  32³  (recommended default — good balance)
//   Quality 4 →  48³
//   Quality 5 →  64³  (slowest, finest — may produce many elements)
//
// Algorithm: surface voxelization via Separating Axis Theorem (SAT)
// triangle–AABB overlap test (Akenine-Möller). Each triangle is tested
// against all voxels in its AABB. This matches what the JS reference
// voxelizer does, rewritten here in C++.
// ─────────────────────────────────────────────────────────────────────────────
class Voxelizer {
public:
    struct Config {
        int quality = 3;
        bool solidFill = false;
        bool verbose = true;

        Config() = default;
    };

    static int qualityToResolution(int quality);

    // Config must be passed explicitly (MinGW limitation with nested struct defaults)
    explicit Voxelizer(Config cfg);

    // Main entry: voxelize a mesh into a grid.
    // Mesh MUST be pre-normalized into [0, 16] MC space.
    VoxelGrid voxelize(const Mesh &mesh) const;

private:
    Config cfg_;

    // SAT triangle-AABB overlap test (Akenine-Möller 2001)
    bool triangleOverlapsVoxel(
        const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2,
        const glm::vec3 &voxelCenter,
        const glm::vec3 &halfSize) const;

    // Barycentric coordinate computation for color sampling
    glm::vec3 barycentricCoords(
        const glm::vec3 &p,
        const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) const;

    // Flood-fill from outside to mark interior voxels as solid
    int floodFillSolid(VoxelGrid &grid) const;
};