#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Mesh — plain data container holding geometry loaded from any 3D file format.
// Format-agnostic; both ObjLoader and GltfLoader produce a Mesh.
// ─────────────────────────────────────────────────────────────────────────────
class Mesh {
public:

    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;       // texture coordinate in [0,1] (0,0 if mesh has none)
        glm::vec3 normal;
    };

    struct Triangle {
        std::array<int, 3> vertexIndices;
        int materialIndex;  // index into materials[], -1 = default white
    };

    // ── Texture image ─────────────────────────────────────────────────────────
    // Decoded RGBA pixel data for a single diffuse texture.
    // Loaded by the file loader (ObjLoader / GltfLoader) when the MTL/glTF
    // references an image file. If empty, sampleColor falls back to baseColor.
    struct TextureImage {
        std::vector<uint8_t> pixels; // RGBA, row-major, top-to-bottom
        int width  = 0;
        int height = 0;
        bool loaded() const { return !pixels.empty() && width > 0 && height > 0; }
    };

    struct Material {
        std::string   name;
        glm::vec3     baseColor  = glm::vec3(1.0f); // flat fallback color (RGB 0-1)
        std::string   texturePath;                  // path to diffuse image, may be empty
        TextureImage  texture;                      // decoded pixels (loaded by loader)
    };

    struct AABB {
        glm::vec3 min, max;
        glm::vec3 size()   const { return max - min; }
        glm::vec3 center() const { return (min + max) * 0.5f; }
    };

    // ── Data ──────────────────────────────────────────────────────────────────
    std::vector<Vertex>   vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;

    // ── Queries ───────────────────────────────────────────────────────────────

    bool isEmpty() const { return vertices.empty() || triangles.empty(); }

    AABB computeBounds() const;

    std::array<Vertex, 3> getTriangleVertices(const Triangle &tri) const;

    // Sample the color for a point on a triangle.
    //
    // barycentricWeights = (w0, w1) where w2 = 1 - w0 - w1.
    // These are the weights for vertexIndices[0] and vertexIndices[1].
    //
    // If the material has a loaded texture:
    //   → interpolates the three vertex UVs with the barycentric weights,
    //     then performs a nearest-neighbor sample of the texture image.
    // Otherwise:
    //   → returns material.baseColor.
    glm::vec3 sampleColor(const Triangle &tri,
                          const glm::vec2 &barycentricWeights) const;
};