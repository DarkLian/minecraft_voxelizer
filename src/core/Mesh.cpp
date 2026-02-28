#include "core/Mesh.hpp"
#include <limits>
#include <stdexcept>
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

// ── Texture sampling ──────────────────────────────────────────────────────────

glm::vec3 Mesh::sampleColor(const Triangle &tri, const glm::vec2 &bary) const {
    // Guard: invalid material
    if (tri.materialIndex < 0 ||
        tri.materialIndex >= static_cast<int>(materials.size()))
        return glm::vec3(1.0f);

    const Material &mat = materials[tri.materialIndex];

    // If no texture is loaded fall back to the flat material colour.
    if (!mat.hasTexture())
        return mat.baseColor;

    // ── Interpolate UV across the triangle ────────────────────────────────────
    // bary.x = weight for vertex 0, bary.y = weight for vertex 1
    // weight for vertex 2 = 1 - bary.x - bary.y
    float w0 = bary.x;
    float w1 = bary.y;
    float w2 = 1.0f - w0 - w1;

    const Vertex &v0 = vertices[tri.vertexIndices[0]];
    const Vertex &v1 = vertices[tri.vertexIndices[1]];
    const Vertex &v2 = vertices[tri.vertexIndices[2]];

    glm::vec2 uv = v0.uv * w0 + v1.uv * w1 + v2.uv * w2;

    // ── Wrap UV into [0, 1) ───────────────────────────────────────────────────
    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);

    // OBJ stores V with 0 at the bottom; image memory has row 0 at the top.
    // glTF 2.0 already uses top-left origin so no flip is needed.
    if (mat.flipV) v = 1.0f - v;

    // ── Bilinear sample ───────────────────────────────────────────────────────
    int W  = mat.imageW;
    int H  = mat.imageH;
    int ch = mat.imageChannels;

    // Sub-pixel position in texel space
    float fx = u * static_cast<float>(W) - 0.5f;
    float fy = v * static_cast<float>(H) - 0.5f;

    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float tx = fx - static_cast<float>(x0);
    float ty = fy - static_cast<float>(y0);

    // Clamp to valid pixel range (repeat wrapping already handled above)
    auto clampCoord = [](int c, int limit) { return std::clamp(c, 0, limit - 1); };
    int x1 = clampCoord(x0 + 1, W);
    int y1 = clampCoord(y0 + 1, H);
    x0 = clampCoord(x0, W);
    y0 = clampCoord(y0, H);

    // Pixel fetch helper: returns RGB in [0,1]
    auto fetchPixel = [&](int px, int py) -> glm::vec3 {
        int idx = (py * W + px) * ch;
        return glm::vec3(
            mat.imageData[idx + 0] / 255.0f,
            mat.imageData[idx + 1] / 255.0f,
            mat.imageData[idx + 2] / 255.0f
        );
    };

    // 2×2 bilinear blend
    glm::vec3 c00 = fetchPixel(x0, y0);
    glm::vec3 c10 = fetchPixel(x1, y0);
    glm::vec3 c01 = fetchPixel(x0, y1);
    glm::vec3 c11 = fetchPixel(x1, y1);

    glm::vec3 top    = glm::mix(c00, c10, tx);
    glm::vec3 bottom = glm::mix(c01, c11, tx);
    return glm::mix(top, bottom, ty);
}