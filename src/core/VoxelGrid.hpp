#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Face direction indices — used throughout the pipeline consistently.
// These match Minecraft's face naming conventions.
// ─────────────────────────────────────────────────────────────────────────────
enum class Face : int {
    Down = 0, // -Y
    Up = 1, // +Y
    North = 2, // -Z
    South = 3, // +Z
    West = 4, // -X
    East = 5 // +X
};

constexpr int FACE_COUNT = 6;

// ─────────────────────────────────────────────────────────────────────────────
// VoxelGrid — 3D grid of voxels produced by the Voxelizer.
// Stores solid flags and per-voxel colors in flat arrays for cache efficiency.
// ─────────────────────────────────────────────────────────────────────────────
class VoxelGrid {
public:
    VoxelGrid(int resX, int resY, int resZ);

    // ── Setters ───────────────────────────────────────────────────────────────

    void setSolid(int x, int y, int z, bool solid);

    void setColor(int x, int y, int z, const glm::vec3 &color);

    // Set both at once (common case in voxelizer)
    void set(int x, int y, int z, const glm::vec3 &color);

    // ── Queries ───────────────────────────────────────────────────────────────

    bool isSolid(int x, int y, int z) const;

    glm::vec3 getColor(int x, int y, int z) const;

    // Returns true when the given face of voxel (x,y,z) borders empty space.
    // Only exposed faces need to be rendered — this is the primary culling step.
    bool isFaceExposed(int x, int y, int z, Face face) const;

    // Bounds check
    bool inBounds(int x, int y, int z) const;

    // ── Dimensions ────────────────────────────────────────────────────────────
    int resX, resY, resZ;

    int totalVoxels() const { return resX * resY * resZ; }

    int solidCount() const;

private:
    // Flat array index: x + y*resX + z*resX*resY
    int idx(int x, int y, int z) const;

    std::vector<glm::vec3> colors_; // per-voxel color (RGB 0-1)
    std::vector<uint8_t> solid_; // 1 = solid, 0 = empty (uint8 for speed)
};