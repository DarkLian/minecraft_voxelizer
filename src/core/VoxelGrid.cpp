#include "core/VoxelGrid.hpp"
#include <stdexcept>

VoxelGrid::VoxelGrid(int resX, int resY, int resZ)
    : resX(resX), resY(resY), resZ(resZ) {
    if (resX <= 0 || resY <= 0 || resZ <= 0)
        throw std::invalid_argument("VoxelGrid: resolution must be positive.");
    int total = resX * resY * resZ;
    colors_.assign(total, glm::vec3(0.0f));
    solid_.assign(total, 0);
    faceTri_.assign(total * FACE_COUNT, -1);
}

int VoxelGrid::idx(int x, int y, int z) const {
    return x + y * resX + z * resX * resY;
}

int VoxelGrid::faceIdx(int x, int y, int z, Face face) const {
    return idx(x, y, z) * FACE_COUNT + static_cast<int>(face);
}

void VoxelGrid::setSolid(int x, int y, int z, bool solid) {
    if (!inBounds(x, y, z)) return;
    solid_[idx(x, y, z)] = solid ? 1 : 0;
}

void VoxelGrid::setColor(int x, int y, int z, const glm::vec3 &color) {
    if (!inBounds(x, y, z)) return;
    colors_[idx(x, y, z)] = color;
}

void VoxelGrid::set(int x, int y, int z, const glm::vec3 &color) {
    if (!inBounds(x, y, z)) return;
    int i = idx(x, y, z);
    solid_[i] = 1;
    colors_[i] = color;
}

void VoxelGrid::setFaceTriIndex(int x, int y, int z, Face face, int triIndex) {
    if (!inBounds(x, y, z)) return;
    faceTri_[faceIdx(x, y, z, face)] = triIndex;
}

void VoxelGrid::setColorAndTri(int x, int y, int z,
                               const glm::vec3 &color, int triIndex) {
    if (!inBounds(x, y, z)) return;
    colors_[idx(x, y, z)] = color;
    // Store as fallback across all face directions that don't have a winner yet
    int base = idx(x, y, z) * FACE_COUNT;
    for (int f = 0; f < FACE_COUNT; f++)
        if (faceTri_[base + f] == -1)
            faceTri_[base + f] = triIndex;
}

bool VoxelGrid::isSolid(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return false;
    return solid_[idx(x, y, z)] != 0;
}

glm::vec3 VoxelGrid::getColor(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return glm::vec3(0.0f);
    return colors_[idx(x, y, z)];
}

int VoxelGrid::getTriIndex(int x, int y, int z, Face face) const {
    if (!inBounds(x, y, z)) return -1;
    return faceTri_[faceIdx(x, y, z, face)];
}

int VoxelGrid::getAnyTriIndex(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return -1;
    int base = idx(x, y, z) * FACE_COUNT;
    for (int f = 0; f < FACE_COUNT; f++)
        if (faceTri_[base + f] != -1)
            return faceTri_[base + f];
    return -1;
}

bool VoxelGrid::isFaceExposed(int x, int y, int z, Face face) const {
    if (!isSolid(x, y, z)) return false;
    int nx = x, ny = y, nz = z;
    switch (face) {
        case Face::Down: ny--;
            break;
        case Face::Up: ny++;
            break;
        case Face::North: nz--;
            break;
        case Face::South: nz++;
            break;
        case Face::West: nx--;
            break;
        case Face::East: nx++;
            break;
    }
    return !isSolid(nx, ny, nz);
}

bool VoxelGrid::inBounds(int x, int y, int z) const {
    return x >= 0 && x < resX &&
           y >= 0 && y < resY &&
           z >= 0 && z < resZ;
}

int VoxelGrid::solidCount() const {
    int count = 0;
    for (auto v: solid_) count += v;
    return count;
}
