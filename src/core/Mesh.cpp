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
// Returns the color for a voxel that overlaps the given triangle.
//
// If the material has a loaded texture, samples it at the triangle's centroid
// UV (the average of the three vertex UVs). The barycentric weights are
// intentionally ignored.
//
// Why centroid instead of interpolated UV?
//   Interpolating per-voxel means adjacent voxels on the same triangle can
//   land on different texture pixels, producing noisy speckled color variation
//   across what should be a single flat surface. At the low pixel counts of a
//   voxelized model this looks worse than wrong — it obscures the actual color.
//   The centroid gives one stable, representative texel per triangle: every
//   voxel that overlaps the same triangle gets the same color, which is clean
//   and consistent with how low-poly / pixel-art 3D models are meant to look.
//
// Texture lookup: nearest-neighbor with UV wrapping.
//   OBJ/glTF UV convention: (0,0) = bottom-left of texture.
//   Image pixel convention: (0,0) = top-left.
//   → V is flipped:  py = (1 - v) * H

glm::vec3 Mesh::sampleColor(const Triangle &tri,
                            const glm::vec2 & /*barycentricWeights*/) const {
    // ── Validate material ─────────────────────────────────────────────────────
    if (tri.materialIndex < 0 ||
        tri.materialIndex >= static_cast<int>(materials.size()))
        return glm::vec3(1.0f);

    const Material &mat = materials[tri.materialIndex];

    // ── No texture: flat base color ───────────────────────────────────────────
    if (!mat.texture.loaded())
        return mat.baseColor;

    // ── Centroid UV ───────────────────────────────────────────────────────────
    const glm::vec2 &uv0 = vertices[tri.vertexIndices[0]].uv;
    const glm::vec2 &uv1 = vertices[tri.vertexIndices[1]].uv;
    const glm::vec2 &uv2 = vertices[tri.vertexIndices[2]].uv;

    glm::vec2 uv = (uv0 + uv1 + uv2) / 3.0f;

    // ── Nearest-neighbor texture sample with UV wrapping ─────────────────────
    const int W = mat.texture.width;
    const int H = mat.texture.height;

    float fu = uv.x - std::floor(uv.x); // wrap to [0, 1)
    float fv = uv.y - std::floor(uv.y);

    int px = static_cast<int>(fu         * static_cast<float>(W));
    int py = static_cast<int>((1.0f - fv) * static_cast<float>(H)); // flip V
    px = std::clamp(px, 0, W - 1);
    py = std::clamp(py, 0, H - 1);

    // ── Read RGBA pixel ───────────────────────────────────────────────────────
    const uint8_t *p = mat.texture.pixels.data() + (py * W + px) * 4;
    return glm::vec3(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
}