#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Face direction indices — Minecraft conventions.
// ─────────────────────────────────────────────────────────────────────────────
enum class Face : int {
    Down  = 0, // -Y
    Up    = 1, // +Y
    North = 2, // -Z
    South = 3, // +Z
    West  = 4, // -X
    East  = 5  // +X
};

constexpr int FACE_COUNT = 6;

// ─────────────────────────────────────────────────────────────────────────────
// VoxelGrid — 3D grid of voxels.
// Stores solid flags, per-voxel colours, and the index of the mesh triangle
// that "won" colour selection for each voxel.  The triangle index is used
// later by McModel to do per-pixel UV interpolation (texture baking).
// ─────────────────────────────────────────────────────────────────────────────
class VoxelGrid {
public:
    VoxelGrid(int resX, int resY, int resZ);

    // ── Setters ───────────────────────────────────────────────────────────────
    void setSolid(int x, int y, int z, bool solid);
    void setColor(int x, int y, int z, const glm::vec3 &color);

    // Set solid + colour + source triangle index in one call.
    void set(int x, int y, int z, const glm::vec3 &color, int triIndex = -1);

    // Update colour + triangle index without touching the solid flag.
    void setColorAndTri(int x, int y, int z, const glm::vec3 &color, int triIndex);

    // ── Queries ───────────────────────────────────────────────────────────────
    bool      isSolid(int x, int y, int z) const;
    glm::vec3 getColor(int x, int y, int z) const;

    // Returns the mesh triangle index that provided this voxel's colour,
    // or -1 if none was recorded (flat-colour material or flood-filled).
    int getTriIndex(int x, int y, int z) const;

    bool isFaceExposed(int x, int y, int z, Face face) const;
    bool inBounds(int x, int y, int z) const;

    // ── Dimensions ────────────────────────────────────────────────────────────
    int resX, resY, resZ;
    int totalVoxels() const { return resX * resY * resZ; }
    int solidCount()  const;

private:
    int idx(int x, int y, int z) const;

    std::vector<glm::vec3> colors_;
    std::vector<uint8_t>   solid_;
    std::vector<int>       triIndex_; // mesh triangle index per voxel (-1 = none)
};