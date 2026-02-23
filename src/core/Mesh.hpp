#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Mesh — plain data container holding geometry loaded from any 3D file format.
// This is format-agnostic; both ObjLoader and GltfLoader produce a Mesh.
// ─────────────────────────────────────────────────────────────────────────────
class Mesh {
public:
    // ── Sub-types ─────────────────────────────────────────────────────────────

    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;        // texture coordinate, may be (0,0) if mesh has none
        glm::vec3 normal;    // surface normal, used for face culling
    };

    struct Triangle {
        std::array<int, 3> vertexIndices;
        int materialIndex;   // index into materials[], -1 = default white
    };

    struct Material {
        std::string name;
        glm::vec3   baseColor  = glm::vec3(1.0f); // fallback flat color (RGB 0-1)
        std::string texturePath;                   // path to diffuse texture, may be empty
    };

    // ── Axis-Aligned Bounding Box helper ──────────────────────────────────────
    struct AABB {
        glm::vec3 min;
        glm::vec3 max;

        glm::vec3 size()   const { return max - min; }
        glm::vec3 center() const { return (min + max) * 0.5f; }
    };

    // ── Data ──────────────────────────────────────────────────────────────────
    std::vector<Vertex>   vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;

    // ── Queries ───────────────────────────────────────────────────────────────

    bool isEmpty() const { return vertices.empty() || triangles.empty(); }

    // Computes tight AABB over all vertex positions
    AABB computeBounds() const;

    // Convenience: get the three vertices of a triangle
    std::array<Vertex, 3> getTriangleVertices(const Triangle& tri) const;

    // Sample the material color at a given triangle + barycentric coordinate.
    // Returns base color since full texture sampling requires image data;
    // the TextureAtlas handles actual per-pixel sampling when textures are loaded.
    glm::vec3 sampleColor(const Triangle& tri,
                           const glm::vec2& barycentricUV) const;
};
