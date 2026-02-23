#include "core/Mesh.hpp"
#include <limits>
#include <cmath>
#include <algorithm>

Mesh::AABB Mesh::computeBounds() const {
    AABB bounds;
    bounds.min = glm::vec3( std::numeric_limits<float>::max());
    bounds.max = glm::vec3(-std::numeric_limits<float>::max());
    for (const auto &v : vertices) {
        bounds.min = glm::min(bounds.min, v.position);
        bounds.max = glm::max(bounds.max, v.position);
    }
    return bounds;
}

std::array<Mesh::Vertex, 3> Mesh::getTriangleVertices(const Triangle &tri) const {
    return {
        vertices[tri.vertexIndices[0]],
        vertices[tri.vertexIndices[1]],
        vertices[tri.vertexIndices[2]]
    };
}

// ── sampleColor ───────────────────────────────────────────────────────────────
//
// barycentricWeights.x = weight for vertexIndices[0]  (called w0)
// barycentricWeights.y = weight for vertexIndices[1]  (called w1)
// w2 = 1 - w0 - w1     = weight for vertexIndices[2]
//
// This matches what Voxelizer passes: vec2(bary.x, bary.y) where
// barycentricCoords() returns (u, v, w) = weights for (a, b, c) = (v0, v1, v2).
//
// If the material has a loaded texture, the interpolated UV is used to
// sample it with nearest-neighbor (wrapping). Otherwise baseColor is returned.

glm::vec3 Mesh::sampleColor(const Triangle &tri,
                            const glm::vec2 &barycentricWeights) const {
    // ── Validate material index ───────────────────────────────────────────────
    if (tri.materialIndex < 0 ||
        tri.materialIndex >= static_cast<int>(materials.size())) {
        return glm::vec3(1.0f);
    }
    const Material &mat = materials[tri.materialIndex];

    // ── No texture: return flat base color ────────────────────────────────────
    if (!mat.texture.loaded())
        return mat.baseColor;

    // ── Interpolate UV from the three vertices ────────────────────────────────
    //
    // Barycentric weights: w0, w1, w2 (sum = 1).
    float w0 = barycentricWeights.x;
    float w1 = barycentricWeights.y;
    float w2 = 1.0f - w0 - w1;

    const glm::vec2 &uv0 = vertices[tri.vertexIndices[0]].uv;
    const glm::vec2 &uv1 = vertices[tri.vertexIndices[1]].uv;
    const glm::vec2 &uv2 = vertices[tri.vertexIndices[2]].uv;

    glm::vec2 uv = uv0 * w0 + uv1 * w1 + uv2 * w2;

    // ── Convert UV to pixel coordinates ──────────────────────────────────────
    //
    // OBJ/OpenGL UV convention: (0,0) = bottom-left, (1,1) = top-right.
    // PNG/image convention:     (0,0) = top-left.
    // → flip V:  py = (1 - uv.y) * height
    //
    // Wrap using fmod so tiling textures work correctly.
    const int W = mat.texture.width;
    const int H = mat.texture.height;

    // Wrap U and V into [0, 1)
    float fu = uv.x - std::floor(uv.x); // fmod that handles negatives
    float fv = uv.y - std::floor(uv.y);

    // Flip V for image-space, clamp to valid pixel range
    int px = static_cast<int>(fu         * static_cast<float>(W));
    int py = static_cast<int>((1.0f - fv) * static_cast<float>(H));
    px = std::clamp(px, 0, W - 1);
    py = std::clamp(py, 0, H - 1);

    // ── Sample RGBA pixel ─────────────────────────────────────────────────────
    int base = (py * W + px) * 4; // RGBA = 4 channels
    const uint8_t *p = mat.texture.pixels.data() + base;

    return glm::vec3(
        static_cast<float>(p[0]) / 255.0f,
        static_cast<float>(p[1]) / 255.0f,
        static_cast<float>(p[2]) / 255.0f
    );
}