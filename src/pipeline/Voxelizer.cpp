#include "pipeline/Voxelizer.hpp"
#include <glm/gtc/epsilon.hpp>
#include <iostream>
#include <algorithm>
#include <queue>
#include <cmath>

// ── Quality resolution mapping ────────────────────────────────────────────────

int Voxelizer::qualityToResolution(int quality) {
    switch (quality) {
        case 1:  return 16;
        case 2:  return 24;
        case 3:  return 32;
        case 4:  return 48;
        case 5:  return 64;
        default: return 32;
    }
}

Voxelizer::Voxelizer(Config cfg) : cfg_(cfg) {}

// ── Main voxelization ─────────────────────────────────────────────────────────

VoxelGrid Voxelizer::voxelize(const Mesh& mesh) const {
    int res = qualityToResolution(cfg_.quality);

    // Minecraft model space is [0, 16]. Voxel size in MC units:
    float voxelSize = 16.0f / static_cast<float>(res);
    glm::vec3 halfSize(voxelSize * 0.5f);

    if (cfg_.verbose) {
        std::cout << "[Voxelizer] Quality " << cfg_.quality
                  << " → " << res << "³ grid, voxel size = "
                  << voxelSize << " MC units\n";
        std::cout << "[Voxelizer] Processing " << mesh.triangles.size()
                  << " triangles...\n";
    }

    VoxelGrid grid(res, res, res);
    int filledCount = 0;

    for (const auto& tri : mesh.triangles) {
        auto verts = mesh.getTriangleVertices(tri);
        glm::vec3 p0 = verts[0].position;
        glm::vec3 p1 = verts[1].position;
        glm::vec3 p2 = verts[2].position;

        // AABB of this triangle in voxel space
        glm::vec3 triMin = glm::min(glm::min(p0, p1), p2);
        glm::vec3 triMax = glm::max(glm::max(p0, p1), p2);

        int xMin = std::max(0, static_cast<int>(std::floor(triMin.x / voxelSize)));
        int yMin = std::max(0, static_cast<int>(std::floor(triMin.y / voxelSize)));
        int zMin = std::max(0, static_cast<int>(std::floor(triMin.z / voxelSize)));
        int xMax = std::min(res - 1, static_cast<int>(std::floor(triMax.x / voxelSize)));
        int yMax = std::min(res - 1, static_cast<int>(std::floor(triMax.y / voxelSize)));
        int zMax = std::min(res - 1, static_cast<int>(std::floor(triMax.z / voxelSize)));

        for (int z = zMin; z <= zMax; z++) {
            for (int y = yMin; y <= yMax; y++) {
                for (int x = xMin; x <= xMax; x++) {
                    if (grid.isSolid(x, y, z)) continue; // already filled

                    glm::vec3 voxelCenter(
                        (x + 0.5f) * voxelSize,
                        (y + 0.5f) * voxelSize,
                        (z + 0.5f) * voxelSize
                    );

                    if (triangleOverlapsVoxel(p0, p1, p2, voxelCenter, halfSize)) {
                        // Sample color at voxel center via barycentric coords
                        glm::vec3 bary = barycentricCoords(voxelCenter, p0, p1, p2);
                        bary = glm::clamp(bary, 0.0f, 1.0f);
                        // Normalize so bary sums to 1
                        float sum = bary.x + bary.y + bary.z;
                        if (sum > 1e-6f) bary /= sum;

                        glm::vec3 color = mesh.sampleColor(tri, glm::vec2(bary.x, bary.y));
                        grid.set(x, y, z, color);
                        filledCount++;
                    }
                }
            }
        }
    }

    if (cfg_.solidFill)
        floodFillSolid(grid);

    if (cfg_.verbose) {
        std::cout << "[Voxelizer] Filled " << filledCount
                  << " / " << grid.totalVoxels() << " voxels ("
                  << (100.0f * filledCount / grid.totalVoxels()) << "%)\n";
    }

    return grid;
}

// ── SAT Triangle–AABB Overlap Test ───────────────────────────────────────────
// Based on Akenine-Möller (2001), "Fast 3D Triangle-Box Overlap Testing"
// Tests 13 separating axes: 3 face normals of box, 1 triangle normal,
// and 9 edge cross-products.

static inline void project(const glm::vec3 axis,
                            const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                            float& minVal, float& maxVal)
{
    float d0 = glm::dot(axis, v0);
    float d1 = glm::dot(axis, v1);
    float d2 = glm::dot(axis, v2);
    minVal = std::min({d0, d1, d2});
    maxVal = std::max({d0, d1, d2});
}

static inline float projectBox(const glm::vec3& axis, const glm::vec3& halfSize) {
    return halfSize.x * std::abs(axis.x)
         + halfSize.y * std::abs(axis.y)
         + halfSize.z * std::abs(axis.z);
}

bool Voxelizer::triangleOverlapsVoxel(
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
    const glm::vec3& voxelCenter,
    const glm::vec3& halfSize) const
{
    // Translate triangle so voxel center is at origin
    glm::vec3 a = v0 - voxelCenter;
    glm::vec3 b = v1 - voxelCenter;
    glm::vec3 c = v2 - voxelCenter;

    glm::vec3 edges[3] = { b - a, c - b, a - c };
    glm::vec3 boxAxes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };

    float minTri, maxTri, boxR;

    // ── 9 edge × box-axis cross products ─────────────────────────────────────
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 axis = glm::cross(edges[i], boxAxes[j]);
            if (glm::dot(axis, axis) < 1e-10f) continue; // degenerate
            project(axis, a, b, c, minTri, maxTri);
            boxR = projectBox(axis, halfSize);
            if (minTri > boxR || maxTri < -boxR) return false;
        }
    }

    // ── 3 box face normals (AABB axes) ───────────────────────────────────────
    for (int i = 0; i < 3; i++) {
        project(boxAxes[i], a, b, c, minTri, maxTri);
        if (minTri > halfSize[i] || maxTri < -halfSize[i]) return false;
    }

    // ── 1 triangle normal ─────────────────────────────────────────────────────
    glm::vec3 normal = glm::cross(edges[0], edges[1]);
    if (glm::dot(normal, normal) > 1e-10f) {
        float d = glm::dot(normal, a);
        boxR    = projectBox(normal, halfSize);
        if (d > boxR || d < -boxR) return false;
    }

    return true;
}

// ── Barycentric coordinates ───────────────────────────────────────────────────

glm::vec3 Voxelizer::barycentricCoords(
    const glm::vec3& p,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) const
{
    glm::vec3 v0 = b - a;
    glm::vec3 v1 = c - a;
    glm::vec3 v2 = p - a;

    float d00 = glm::dot(v0, v0);
    float d01 = glm::dot(v0, v1);
    float d11 = glm::dot(v1, v1);
    float d20 = glm::dot(v2, v0);
    float d21 = glm::dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;

    if (std::abs(denom) < 1e-10f)
        return glm::vec3(1.0f / 3.0f); // degenerate: return centroid

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;
    return glm::vec3(u, v, w);
}

// ── Flood-fill interior ───────────────────────────────────────────────────────
// Marks all voxels reachable from outside as "air", then inverts:
// anything NOT reachable = interior = solid.

void Voxelizer::floodFillSolid(VoxelGrid& grid) const {
    int rx = grid.resX, ry = grid.resY, rz = grid.resZ;

    // visited[x + y*rx + z*rx*ry]
    std::vector<uint8_t> visited(rx * ry * rz, 0);
    auto idx = [&](int x, int y, int z){ return x + y*rx + z*rx*ry; };

    std::queue<glm::ivec3> q;

    // Seed all border voxels that are not solid
    for (int z = 0; z < rz; z++)
    for (int y = 0; y < ry; y++)
    for (int x = 0; x < rx; x++) {
        if ((x == 0 || x == rx-1 || y == 0 || y == ry-1 || z == 0 || z == rz-1)
            && !grid.isSolid(x, y, z))
        {
            visited[idx(x,y,z)] = 1;
            q.push({x, y, z});
        }
    }

    const glm::ivec3 dirs[6] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    };

    while (!q.empty()) {
        glm::ivec3 cur = q.front(); q.pop();
        int x = cur.x, y = cur.y, z = cur.z;
        for (auto& d : dirs) {
            int nx = x+d.x, ny = y+d.y, nz = z+d.z;
            if (!grid.inBounds(nx,ny,nz)) continue;
            if (visited[idx(nx,ny,nz)]) continue;
            if (grid.isSolid(nx,ny,nz)) continue;
            visited[idx(nx,ny,nz)] = 1;
            q.push(glm::ivec3(nx, ny, nz));
        }
    }

    // Any unvisited non-solid voxel is interior → fill it
    int filled = 0;
    for (int z = 0; z < rz; z++)
    for (int y = 0; y < ry; y++)
    for (int x = 0; x < rx; x++) {
        if (!visited[idx(x,y,z)] && !grid.isSolid(x,y,z)) {
            // Sample color from nearest solid neighbor (simple approach)
            grid.set(x, y, z, glm::vec3(0.8f));
            filled++;
        }
    }

    if (cfg_.verbose)
        std::cout << "[Voxelizer] Flood-fill: " << filled << " interior voxels filled.\n";
}
