#include "pipeline/Voxelizer.hpp"
#include <glm/gtc/epsilon.hpp>
#include <iostream>
#include <algorithm>
#include <queue>
#include <cmath>

int Voxelizer::qualityToResolution(int quality) {
    switch (quality) {
        case 1: return 16;
        case 2: return 24;
        case 3: return 32;
        case 4: return 48;
        case 5: return 64;
        case 6: return 96;
        case 7: return 128;
        default: return 32;
    }
}

Voxelizer::Voxelizer(Config cfg) : cfg_(cfg) {}

// ─────────────────────────────────────────────────────────────────────────────
// voxelize — surface voxelisation with per-face-direction triangle selection.
//
// The key insight for layered anime geometry (hair over face, eyes on face skin):
//   Each voxel stores up to 6 triangle indices, one per face direction.
//   For each triangle we compute its unit normal and record it as a candidate
//   for every face direction F where dot(triNormal, faceNormal(F)) > threshold.
//   Within each face direction, the closest triangle wins (distance to plane).
//
//   Result: eye-area voxels store the face-skin triangle for their South/front
//   face and the hair triangle for their Up face — both are used correctly
//   when the atlas is baked, because each exposed quad knows its own face dir.
// ─────────────────────────────────────────────────────────────────────────────
VoxelGrid Voxelizer::voxelize(const Mesh &mesh) const {
    int res = qualityToResolution(cfg_.quality);
    float voxelSize = 16.0f / static_cast<float>(res);
    glm::vec3 halfSize(voxelSize * 0.5f);

    if (cfg_.verbose) {
        std::cout << "[Voxelizer] Quality " << cfg_.quality
                  << " -> " << res << "^3 grid, voxel size = "
                  << voxelSize << " MC units\n";
        std::cout << "[Voxelizer] Processing " << mesh.triangles.size()
                  << " triangles...\n";
    }

    VoxelGrid grid(res, res, res);

    // Per-voxel per-face: best distance seen so far (lower = closer = wins).
    // Layout: [voxelIdx * FACE_COUNT + faceIdx]
    const float kInf = 1e9f;
    int total = res * res * res;
    std::vector<float> bestDist(static_cast<size_t>(total) * FACE_COUNT, kInf);
    // Per-voxel overall: for the solid flag / fallback colour
    std::vector<float> bestDistAny(total, kInf);

    // Helper: flat index into bestDist
    auto di = [&](int x, int y, int z, int f) {
        return (x + y * res + z * res * res) * FACE_COUNT + f;
    };
    auto ai = [&](int x, int y, int z) {
        return x + y * res + z * res * res;
    };

    // Face direction unit normals (matches Face enum order)
    const glm::vec3 faceNormals[FACE_COUNT] = {
        { 0, -1,  0}, // Down
        { 0,  1,  0}, // Up
        { 0,  0, -1}, // North
        { 0,  0,  1}, // South
        {-1,  0,  0}, // West
        { 1,  0,  0}, // East
    };

    // Distance from point p to plane of triangle (a,b,c)
    auto distToPlane = [](const glm::vec3 &p,
                          const glm::vec3 &a,
                          const glm::vec3 &b,
                          const glm::vec3 &c) -> float {
        glm::vec3 n = glm::cross(b - a, c - a);
        float len = glm::length(n);
        if (len < 1e-10f) return 1e9f;
        return std::abs(glm::dot(n / len, p - a));
    };

    // Dot-product threshold: triangle contributes to a face direction if its
    // normal has at least this much alignment with that face's outward normal.
    // 0.0 = any front-facing; 0.3 = within ~72 degrees.
    // We use a low threshold so slightly angled faces still get recorded.
    constexpr float kNormalThreshold = 0.1f;

    int filledCount = 0;

    int triIdx = 0;
    for (const auto &tri : mesh.triangles) {
        auto verts = mesh.getTriangleVertices(tri);
        const glm::vec3 &p0 = verts[0].position;
        const glm::vec3 &p1 = verts[1].position;
        const glm::vec3 &p2 = verts[2].position;

        // Skip degenerate / zero-area triangles (UV seam connectors etc.)
        glm::vec3 rawNormal = glm::cross(p1 - p0, p2 - p0);
        float area = glm::length(rawNormal);
        if (area < 1e-6f) { triIdx++; continue; }
        glm::vec3 triNormal = rawNormal / area; // unit normal

        // Which face directions does this triangle face toward?
        // Build a small list of (faceIndex, dotProduct) pairs above threshold.
        int   candidateFaces[FACE_COUNT];
        int   candidateCount = 0;
        for (int f = 0; f < FACE_COUNT; f++) {
            float d = glm::dot(triNormal, faceNormals[f]);
            if (d > kNormalThreshold)
                candidateFaces[candidateCount++] = f;
        }
        // If the triangle is edge-on to all face directions (unlikely but safe)
        // still allow it as a fallback by adding the best-matching face.
        if (candidateCount == 0) {
            float best = -1e9f; int bestF = 0;
            for (int f = 0; f < FACE_COUNT; f++) {
                float d = glm::dot(triNormal, faceNormals[f]);
                if (d > best) { best = d; bestF = f; }
            }
            candidateFaces[candidateCount++] = bestF;
        }

        // AABB of triangle in voxel grid
        glm::vec3 triMin = glm::min(glm::min(p0, p1), p2);
        glm::vec3 triMax = glm::max(glm::max(p0, p1), p2);
        int xMin = std::max(0,       (int)std::floor(triMin.x / voxelSize));
        int yMin = std::max(0,       (int)std::floor(triMin.y / voxelSize));
        int zMin = std::max(0,       (int)std::floor(triMin.z / voxelSize));
        int xMax = std::min(res - 1, (int)std::floor(triMax.x / voxelSize));
        int yMax = std::min(res - 1, (int)std::floor(triMax.y / voxelSize));
        int zMax = std::min(res - 1, (int)std::floor(triMax.z / voxelSize));

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

            float dist = distToPlane(voxelCenter, p0, p1, p2);

            // ── Per-face-direction selection ───────────────────────────────
            // Only update face slots this triangle is eligible for.
            for (int ci = 0; ci < candidateCount; ci++) {
                int f  = candidateFaces[ci];
                int fi = di(x, y, z, f);
                if (dist < bestDist[fi] - 1e-5f) {
                    bestDist[fi] = dist;
                    grid.setFaceTriIndex(x, y, z, static_cast<Face>(f), triIdx);
                } else if (dist < bestDist[fi] + 1e-5f) {
                    // Coplanar: luminance tiebreaker for same-surface triangles
                    glm::vec3 bary = barycentricCoords(voxelCenter, p0, p1, p2);
                    bary = glm::clamp(bary, 0.0f, 1.0f);
                    float s = bary.x + bary.y + bary.z;
                    if (s > 1e-6f) bary /= s;
                    glm::vec3 newCol = mesh.sampleColor(tri, glm::vec2(bary.x, bary.y));
                    float newLum = newCol.r * 0.299f + newCol.g * 0.587f + newCol.b * 0.114f;

                    int existingTri = grid.getTriIndex(x, y, z, static_cast<Face>(f));
                    if (existingTri >= 0) {
                        const auto &exTri = mesh.triangles[existingTri];
                        glm::vec3 exCol = mesh.sampleColor(exTri, glm::vec2(bary.x, bary.y));
                        float exLum = exCol.r * 0.299f + exCol.g * 0.587f + exCol.b * 0.114f;
                        if (newLum > exLum + 0.02f) {
                            grid.setFaceTriIndex(x, y, z, static_cast<Face>(f), triIdx);
                        }
                    }
                }
            }

            // ── Overall voxel solid + fallback colour ──────────────────────
            // Always sample the real colour here — used as fallback when
            // per-face baking has no triangle recorded for a given face.
            glm::vec3 bary = barycentricCoords(voxelCenter, p0, p1, p2);
            bary = glm::clamp(bary, 0.0f, 1.0f);
            float s = bary.x + bary.y + bary.z;
            if (s > 1e-6f) bary /= s;
            glm::vec3 color = mesh.sampleColor(tri, glm::vec2(bary.x, bary.y));

            int aidx = ai(x, y, z);
            if (!grid.isSolid(x, y, z)) {
                grid.set(x, y, z, color);   // real colour, not grey placeholder
                filledCount++;
                bestDistAny[aidx] = dist;
            } else if (dist < bestDistAny[aidx] - 1e-5f) {
                grid.setColor(x, y, z, color);
                bestDistAny[aidx] = dist;
            }
        }}}
        triIdx++;
    }

    if (cfg_.solidFill)
        floodFillSolid(grid);

    if (cfg_.verbose) {
        int solid = grid.solidCount();
        std::cout << "[Voxelizer] Grid:     " << res << "^3 = "
                  << grid.totalVoxels() << " total voxels\n"
                  << "[Voxelizer] Filled:   " << solid
                  << " solid voxels ("
                  << (100.0f * solid / grid.totalVoxels()) << "% of grid)\n"
                  << "[Voxelizer] (Element count is reported later after greedy merge)\n";
    }

    return grid;
}

// ── SAT Triangle-AABB Overlap (Akenine-Moller 2001) ──────────────────────────

static inline void project(const glm::vec3 axis,
                           const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2,
                           float &mn, float &mx) {
    float d0 = glm::dot(axis, v0);
    float d1 = glm::dot(axis, v1);
    float d2 = glm::dot(axis, v2);
    mn = std::min({d0, d1, d2});
    mx = std::max({d0, d1, d2});
}

static inline float projectBox(const glm::vec3 &axis, const glm::vec3 &h) {
    return h.x * std::abs(axis.x) + h.y * std::abs(axis.y) + h.z * std::abs(axis.z);
}

bool Voxelizer::triangleOverlapsVoxel(
    const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2,
    const glm::vec3 &voxelCenter, const glm::vec3 &halfSize) const {

    glm::vec3 a = v0 - voxelCenter;
    glm::vec3 b = v1 - voxelCenter;
    glm::vec3 c = v2 - voxelCenter;
    glm::vec3 edges[3]   = {b - a, c - b, a - c};
    glm::vec3 boxAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};

    float mn, mx, boxR;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 axis = glm::cross(edges[i], boxAxes[j]);
            if (glm::dot(axis, axis) < 1e-10f) continue;
            project(axis, a, b, c, mn, mx);
            boxR = projectBox(axis, halfSize);
            if (mn > boxR || mx < -boxR) return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        project(boxAxes[i], a, b, c, mn, mx);
        if (mn > halfSize[i] || mx < -halfSize[i]) return false;
    }
    glm::vec3 normal = glm::cross(edges[0], -edges[2]);
    if (glm::dot(normal, normal) > 1e-10f) {
        float d = glm::dot(normal, a);
        boxR = projectBox(normal, halfSize);
        if (d > boxR || d < -boxR) return false;
    }
    return true;
}

// ── Barycentric coords ────────────────────────────────────────────────────────

glm::vec3 Voxelizer::barycentricCoords(
    const glm::vec3 &p,
    const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) const {

    glm::vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1);
    float d11 = glm::dot(v1, v1), d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-10f) return glm::vec3(1.0f / 3.0f);
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    return glm::vec3(1.0f - v - w, v, w);
}

// ── Flood-fill interior ───────────────────────────────────────────────────────

int Voxelizer::floodFillSolid(VoxelGrid &grid) const {
    int rx = grid.resX, ry = grid.resY, rz = grid.resZ;
    std::vector<uint8_t> visited(static_cast<size_t>(rx) * ry * rz, 0);
    auto vidx = [&](int x, int y, int z){ return x + y*rx + z*rx*ry; };

    std::queue<glm::ivec3> q;
    for (int z = 0; z < rz; z++)
    for (int y = 0; y < ry; y++)
    for (int x = 0; x < rx; x++)
        if ((x==0||x==rx-1||y==0||y==ry-1||z==0||z==rz-1) && !grid.isSolid(x,y,z)) {
            visited[vidx(x,y,z)] = 1;
            q.push({x,y,z});
        }

    const glm::ivec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while (!q.empty()) {
        glm::ivec3 cur = q.front(); q.pop();
        int x = cur.x, y = cur.y, z = cur.z;
        for (auto &d : dirs) {
            int nx=x+d.x, ny=y+d.y, nz=z+d.z;
            if (!grid.inBounds(nx,ny,nz)) continue;
            if (visited[vidx(nx,ny,nz)] || grid.isSolid(nx,ny,nz)) continue;
            visited[vidx(nx,ny,nz)] = 1;
            q.push(glm::ivec3(nx,ny,nz));
        }
    }

    int filled = 0;
    for (int z = 0; z < rz; z++)
    for (int y = 0; y < ry; y++)
    for (int x = 0; x < rx; x++)
        if (!visited[vidx(x,y,z)] && !grid.isSolid(x,y,z)) {
            grid.set(x, y, z, glm::vec3(0.8f));
            filled++;
        }

    if (cfg_.verbose)
        std::cout << "[Voxelizer] Flood-fill: " << filled << " interior voxels filled.\n";
    return filled;
}