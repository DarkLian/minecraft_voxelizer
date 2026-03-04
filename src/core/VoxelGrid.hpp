#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

enum class Face : int {
    Down = 0, // -Y
    Up = 1, // +Y
    North = 2, // -Z
    South = 3, // +Z
    West = 4, // -X
    East = 5 // +X
};

constexpr int FACE_COUNT = 6;

// Unit outward normals for each face direction
inline glm::vec3 faceNormal(Face f) {
    switch (f) {
        case Face::Down: return {0, -1, 0};
        case Face::Up: return {0, 1, 0};
        case Face::North: return {0, 0, -1};
        case Face::South: return {0, 0, 1};
        case Face::West: return {-1, 0, 0};
        case Face::East: return {1, 0, 0};
    }
    return {0, 1, 0};
}

// ─────────────────────────────────────────────────────────────────────────────
// VoxelGrid — stores solid flags, per-voxel flat color (fallback), and
// per-face-direction triangle indices for texture baking.
//
// Per-face triangle storage is the key insight for layered geometry:
//   - Eye voxels: South face → face skin triangle (with eye UV)
//                 Up face    → hair triangle (with hair UV)
//   - Without this, the closer hair overwrites the eye on all faces.
// ─────────────────────────────────────────────────────────────────────────────
class VoxelGrid {
public:
    VoxelGrid(int resX, int resY, int resZ);

    // ── Setters ───────────────────────────────────────────────────────────────
    void setSolid(int x, int y, int z, bool solid);

    void setColor(int x, int y, int z, const glm::vec3 &color);

    // Set solid + fallback color.  triIndex stored separately per face.
    void set(int x, int y, int z, const glm::vec3 &color);

    // Record the winning triangle for a specific face direction on this voxel.
    void setFaceTriIndex(int x, int y, int z, Face face, int triIndex);

    // Update fallback color (used when no per-face triangle is available).
    void setColorAndTri(int x, int y, int z, const glm::vec3 &color, int triIndex);

    // ── Queries ───────────────────────────────────────────────────────────────
    [[nodiscard]] bool isSolid(int x, int y, int z) const;

    [[nodiscard]] glm::vec3 getColor(int x, int y, int z) const;

    // Triangle index for the given face direction, or -1 if none recorded.
    [[nodiscard]] int getTriIndex(int x, int y, int z, Face face) const;

    // Fallback: best triangle regardless of face direction (-1 if none).
    [[nodiscard]] int getAnyTriIndex(int x, int y, int z) const;

    [[nodiscard]] bool isFaceExposed(int x, int y, int z, Face face) const;

    [[nodiscard]] bool inBounds(int x, int y, int z) const;

    int resX, resY, resZ;
    [[nodiscard]] int totalVoxels() const { return resX * resY * resZ; }

    [[nodiscard]] int solidCount() const;

private:
    [[nodiscard]] int idx(int x, int y, int z) const;

    [[nodiscard]] int faceIdx(int x, int y, int z, Face face) const;

    std::vector<glm::vec3> colors_;
    std::vector<uint8_t> solid_;
    // Per-face triangle index: size = FACE_COUNT * totalVoxels
    std::vector<int> faceTri_;
};