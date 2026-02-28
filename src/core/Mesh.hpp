#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Mesh — plain data container holding geometry loaded from any 3D file format.
// This is format-agnostic; both ObjLoader and GltfLoader produce a Mesh.
// ─────────────────────────────────────────────────────────────────────────────
class Mesh {
public:
    // ── Sub-types ─────────────────────────────────────────────────────────────

    struct Vertex {
        glm::vec3 position;
        glm::vec2 uv;     // texture coordinate
        glm::vec3 normal; // surface normal
    };

    struct Triangle {
        std::array<int, 3> vertexIndices;
        int materialIndex; // index into materials[], -1 = default white
    };

    struct Material {
        std::string name;
        glm::vec3   baseColor   = glm::vec3(1.0f); // flat fallback colour (RGB 0-1)
        std::string texturePath;                    // resolved path, may be empty

        // Decoded texture pixels (RGB or RGBA, row-major, row 0 = top).
        // Empty when no texture was loaded or the file could not be read.
        std::vector<uint8_t> imageData;
        int imageW        = 0;
        int imageH        = 0;
        int imageChannels = 3;  // 3 = RGB, 4 = RGBA

        // OBJ: UVs have V=0 at bottom → flipV = true.
        // glTF 2.0: UVs have V=0 at top  → flipV = false.
        bool flipV = false;

        bool hasTexture() const { return !imageData.empty() && imageW > 0 && imageH > 0; }
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

    // Computes tight AABB over all vertex positions.
    AABB computeBounds() const;

    // Convenience: get the three vertices of a triangle.
    std::array<Vertex, 3> getTriangleVertices(const Triangle &tri) const;

    // Sample the colour at a given triangle + barycentric weights (bary.x for
    // vertex 0, bary.y for vertex 1, implied bary.z = 1 - x - y for vertex 2).
    // If the material has a decoded texture image the UV is interpolated and
    // sampled; otherwise the flat baseColor is returned.
    glm::vec3 sampleColor(const Triangle &tri,
                          const glm::vec2 &bary) const;
};