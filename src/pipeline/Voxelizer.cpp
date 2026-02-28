#include "pipeline/Voxelizer.hpp"
#include <glm/gtc/epsilon.hpp>
#include <iostream>
#include <algorithm>
#include <queue>
#include <cmath>

// ── Quality resolution mapping ────────────────────────────────────────────────

int Voxelizer::qualityToResolution(int quality) {
    switch (quality) {
        case 1: return 16;
        case 2: return 24;
        case 3: return 32;
        case 4: return 48;
        case 5: return 64;
        default: return 32;
    }
}

Voxelizer::Voxelizer(Config cfg) : cfg_(cfg) {}

// ── Main voxelization ─────────────────────────────────────────────────────────

VoxelGrid Voxelizer::voxelize(const Mesh &mesh) const {
    int res = qualityToResolution(cfg_.quality);

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

    // Per-voxel: distance from voxel center to the winning triangle's plane.
    // The CLOSEST surface wins — this correctly handles layered geometry such
    // as eyes (dark, close) sitting in front of face skin (bright, behind).
    // Luminance-based selection was wrong: it actively discarded dark eye/pupil
    // triangles in favour of the brighter skin geometry behind them.
    const float kInfDist = 1e9f;
    std::vector<float> bestDist(static_cast<size_t>(res) * res * res, kInfDist);
    std::vector<int>   bestTri (static_cast<size_t>(res) * res * res, -1);
    auto gridIdx = [&](int x, int y, int z) {
        return x + y * res + z * res * res;
    };

    // Helper: distance from point p to the plane of triangle (a,b,c).
    // Returns the absolute perpendicular distance.
    auto distToPlane = [](const glm::vec3 &p,
                          const glm::vec3 &a,
                          const glm::vec3 &b,
                          const glm::vec3 &c) -> float {
        glm::vec3 n = glm::cross(b - a, c - a);
        float len = glm::length(n);
        if (len < 1e-10f) return 1e9f;
        return std::abs(glm::dot(n / len, p - a));
    };

    int filledCount = 0;
    int blackVoxels = 0;

    int triIdx = 0;
    for (const auto &tri : mesh.triangles) {
        auto verts = mesh.getTriangleVertices(tri);
        glm::vec3 p0 = verts[0].position;
        glm::vec3 p1 = verts[1].position;
        glm::vec3 p2 = verts[2].position;

        // Skip degenerate triangles: UV seam connectors and zero-area quads
        // have near-zero cross-product length.  They sample UV island borders
        // (usually black) and would corrupt voxels they touch.
        glm::vec3 triNormal = glm::cross(p1 - p0, p2 - p0);
        float triArea = glm::length(triNormal);
        if (triArea < 1e-6f) { triIdx++; continue; }

        // AABB of this triangle in voxel space
        glm::vec3 triMin = glm::min(glm::min(p0, p1), p2);
        glm::vec3 triMax = glm::max(glm::max(p0, p1), p2);

        int xMin = std::max(0,       static_cast<int>(std::floor(triMin.x / voxelSize)));
        int yMin = std::max(0,       static_cast<int>(std::floor(triMin.y / voxelSize)));
        int zMin = std::max(0,       static_cast<int>(std::floor(triMin.z / voxelSize)));
        int xMax = std::min(res - 1, static_cast<int>(std::floor(triMax.x / voxelSize)));
        int yMax = std::min(res - 1, static_cast<int>(std::floor(triMax.y / voxelSize)));
        int zMax = std::min(res - 1, static_cast<int>(std::floor(triMax.z / voxelSize)));

        for (int z = zMin; z <= zMax; z++) {
            for (int y = yMin; y <= yMax; y++) {
                for (int x = xMin; x <= xMax; x++) {
                    glm::vec3 voxelCenter(
                        (x + 0.5f) * voxelSize,
                        (y + 0.5f) * voxelSize,
                        (z + 0.5f) * voxelSize
                    );

                    if (!triangleOverlapsVoxel(p0, p1, p2, voxelCenter, halfSize))
                        continue;

                    // Distance from voxel center to this triangle's plane.
                    // Closer surface = this triangle is more "on top" visually.
                    float dist = distToPlane(voxelCenter, p0, p1, p2);

                    int li = gridIdx(x, y, z);

                    if (!grid.isSolid(x, y, z) || dist < bestDist[li] - 1e-5f) {
                        // First hit, or geometrically closer surface — sample and store.
                        glm::vec3 bary = barycentricCoords(voxelCenter, p0, p1, p2);
                        bary = glm::clamp(bary, 0.0f, 1.0f);
                        float sum = bary.x + bary.y + bary.z;
                        if (sum > 1e-6f) bary /= sum;

                        glm::vec3 color = mesh.sampleColor(tri, glm::vec2(bary.x, bary.y));

                        if (!grid.isSolid(x, y, z)) {
                            grid.set(x, y, z, color, triIdx);
                            filledCount++;
                        } else {
                            grid.setColorAndTri(x, y, z, color, triIdx);
                        }
                        bestDist[li] = dist;
                        bestTri[li]  = triIdx;

                    } else if (dist < bestDist[li] + 1e-5f) {
                        // Coplanar tie (same surface, different triangle).
                        // Use luminance as secondary tiebreaker: prefer the
                        // brighter sample, which is more likely to be a real
                        // surface texel rather than a UV seam border pixel.
                        glm::vec3 bary = barycentricCoords(voxelCenter, p0, p1, p2);
                        bary = glm::clamp(bary, 0.0f, 1.0f);
                        float sum = bary.x + bary.y + bary.z;
                        if (sum > 1e-6f) bary /= sum;

                        glm::vec3 color = mesh.sampleColor(tri, glm::vec2(bary.x, bary.y));
                        float newLum = color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;

                        glm::vec3 existing = grid.getColor(x, y, z);
                        float oldLum = existing.r * 0.299f + existing.g * 0.587f + existing.b * 0.114f;

                        if (newLum > oldLum + 0.02f) {
                            grid.setColorAndTri(x, y, z, color, triIdx);
                            // bestDist stays the same (coplanar)
                            bestTri[li] = triIdx;
                        }
                    }
                }
            }
        }
        triIdx++;
    }

    if (cfg_.solidFill)
        floodFillSolid(grid);

    // ── Diagnostic ────────────────────────────────────────────────────────────
    if (cfg_.verbose) {
        // Count voxels that are very dark (may indicate missing texture)
        for (int z = 0; z < res; z++)
            for (int y = 0; y < res; y++)
                for (int x = 0; x < res; x++) {
                    if (!grid.isSolid(x, y, z)) continue;
                    const glm::vec3 c = grid.getColor(x, y, z);
                    if (c.r < 0.02f && c.g < 0.02f && c.b < 0.02f)
                        blackVoxels++;
                }

        std::cout << "[Voxelizer] Filled " << filledCount
                  << " / " << grid.totalVoxels() << " voxels ("
                  << (100.0f * filledCount / grid.totalVoxels()) << "%)\n";
        if (blackVoxels > 0)
            std::cout << "[Voxelizer] NOTE: " << blackVoxels
                      << " solid voxels are near-black — this is fine if the"
                      << " model has dark eyes/pupils; check loader output if"
                      << " unexpected.\n";
    }

    return grid;
}

// ── SAT Triangle–AABB Overlap (Akenine-Möller 2001) ──────────────────────────

static inline void project(const glm::vec3  axis,
                           const glm::vec3 &v0,
                           const glm::vec3 &v1,
                           const glm::vec3 &v2,
                           float &minVal, float &maxVal) {
    float d0 = glm::dot(axis, v0);
    float d1 = glm::dot(axis, v1);
    float d2 = glm::dot(axis, v2);
    minVal = std::min({d0, d1, d2});
    maxVal = std::max({d0, d1, d2});
}

static inline float projectBox(const glm::vec3 &axis, const glm::vec3 &halfSize) {
    return halfSize.x * std::abs(axis.x)
         + halfSize.y * std::abs(axis.y)
         + halfSize.z * std::abs(axis.z);
}

bool Voxelizer::triangleOverlapsVoxel(
    const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2,
    const glm::vec3 &voxelCenter,
    const glm::vec3 &halfSize) const {

    glm::vec3 a = v0 - voxelCenter;
    glm::vec3 b = v1 - voxelCenter;
    glm::vec3 c = v2 - voxelCenter;

    glm::vec3 edges[3]   = {b - a, c - b, a - c};
    glm::vec3 boxAxes[3] = {{1,0,0}, {0,1,0}, {0,0,1}};

    float minTri, maxTri, boxR;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 axis = glm::cross(edges[i], boxAxes[j]);
            if (glm::dot(axis, axis) < 1e-10f) continue;
            project(axis, a, b, c, minTri, maxTri);
            boxR = projectBox(axis, halfSize);
            if (minTri > boxR || maxTri < -boxR) return false;
        }
    }

    for (int i = 0; i < 3; i++) {
        project(boxAxes[i], a, b, c, minTri, maxTri);
        if (minTri > halfSize[i] || maxTri < -halfSize[i]) return false;
    }

    glm::vec3 normal = glm::cross(edges[0], -edges[2]);
    if (glm::dot(normal, normal) > 1e-10f) {
        float d  = glm::dot(normal, a);
        boxR = projectBox(normal, halfSize);
        if (d > boxR || d < -boxR) return false;
    }

    return true;
}

// ── Barycentric coordinates ───────────────────────────────────────────────────
// Cramér's rule: implicitly projects p onto the triangle plane.

glm::vec3 Voxelizer::barycentricCoords(
    const glm::vec3 &p,
    const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) const {

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
        return glm::vec3(1.0f / 3.0f); // degenerate triangle → centroid

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    return glm::vec3(1.0f - v - w, v, w);
}

// ── Flood-fill interior ───────────────────────────────────────────────────────

int Voxelizer::floodFillSolid(VoxelGrid &grid) const {
    int rx = grid.resX, ry = grid.resY, rz = grid.resZ;

    std::vector<uint8_t> visited(static_cast<size_t>(rx) * ry * rz, 0);
    auto idx = [&](int x, int y, int z) { return x + y * rx + z * rx * ry; };

    std::queue<glm::ivec3> q;

    for (int z = 0; z < rz; z++)
        for (int y = 0; y < ry; y++)
            for (int x = 0; x < rx; x++) {
                if ((x==0 || x==rx-1 || y==0 || y==ry-1 || z==0 || z==rz-1)
                    && !grid.isSolid(x, y, z)) {
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
        for (auto &d : dirs) {
            int nx=x+d.x, ny=y+d.y, nz=z+d.z;
            if (!grid.inBounds(nx,ny,nz)) continue;
            if (visited[idx(nx,ny,nz)]) continue;
            if (grid.isSolid(nx,ny,nz)) continue;
            visited[idx(nx,ny,nz)] = 1;
            q.push(glm::ivec3(nx,ny,nz));
        }
    }

    int filled = 0;
    for (int z = 0; z < rz; z++)
        for (int y = 0; y < ry; y++)
            for (int x = 0; x < rx; x++)
                if (!visited[idx(x,y,z)] && !grid.isSolid(x,y,z)) {
                    grid.set(x, y, z, glm::vec3(0.8f));
                    filled++;
                }

    if (cfg_.verbose)
        std::cout << "[Voxelizer] Flood-fill: " << filled << " interior voxels filled.\n";
    return filled;
}