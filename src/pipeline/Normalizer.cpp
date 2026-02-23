#include "pipeline/Normalizer.hpp"
#include <iostream>

Normalizer::Normalizer(Config cfg) : cfg_(cfg) {
}

Mesh Normalizer::normalize(const Mesh &input) const {
    if (input.isEmpty())
        return input;

    Mesh::AABB bounds = input.computeBounds();
    glm::vec3 size = bounds.size();

    // Find the largest axis for uniform scaling
    float maxDim = glm::max(glm::max(size.x, size.y), size.z);
    if (maxDim < 1e-7f) {
        std::cerr << "[Normalizer] Warning: degenerate mesh (near-zero size).\n";
        return input;
    }

    float targetSize = cfg_.mcUnits - cfg_.padding * 2.0f;
    float scale = targetSize / maxDim;

    // Center of the original mesh
    glm::vec3 center = bounds.center();

    Mesh output = input; // copy materials and triangle indices

    for (auto &v: output.vertices) {
        // 1. Center the mesh at origin
        glm::vec3 p = (v.position - center) * scale;

        // 2. Shift to positive space: center of MC box
        p += glm::vec3(cfg_.mcUnits * 0.5f);

        // 3. If snapping to floor, lift so y_min = 0
        // (applied below after finding new y_min)
        v.position = p;
    }

    if (cfg_.snapFloor) {
        // Find minimum Y after transform
        float yMin = std::numeric_limits<float>::max();
        for (const auto &v: output.vertices)
            yMin = std::min(yMin, v.position.y);

        // Shift all vertices up so the bottom sits at y=0
        float lift = -yMin + cfg_.padding;
        for (auto &v: output.vertices)
            v.position.y += lift;
    }

    // Print stats
    Mesh::AABB newBounds = output.computeBounds();
    std::cout << "[Normalizer] Mesh normalized. "
            << "Scale: x" << scale << "  "
            << "Bounds: ["
            << newBounds.min.x << "," << newBounds.min.y << "," << newBounds.min.z
            << "] → ["
            << newBounds.max.x << "," << newBounds.max.y << "," << newBounds.max.z
            << "]\n";

    return output;
}