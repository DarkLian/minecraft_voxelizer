#include "core/Mesh.hpp"
#include <limits>
#include <stdexcept>

Mesh::AABB Mesh::computeBounds() const {
    AABB bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(-std::numeric_limits<float>::max());

    for (const auto &v: vertices) {
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

glm::vec3 Mesh::sampleColor(const Triangle &tri,
                            const glm::vec2 & /*barycentricUV*/) const {
    // If no materials or invalid index, return white
    if (tri.materialIndex < 0 ||
        tri.materialIndex >= static_cast<int>(materials.size())) {
        return glm::vec3(1.0f);
    }
    return materials[tri.materialIndex].baseColor;
}