#pragma once

#include "core/Mesh.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Normalizer — scales and centers a mesh so it fits within Minecraft's
// model coordinate space: [0, mcUnits] on all axes (default: 16 units).
//
// The mesh is scaled uniformly (preserving aspect ratio) and centered.
// Y-axis is optionally snapped to floor (y_min = 0) so the model sits on
// the ground plane, which is typical for Minecraft block/item models.
// ─────────────────────────────────────────────────────────────────────────────
class Normalizer {
public:
    struct Config {
        float mcUnits = 16.0f; // target bounding box size in MC units
        bool snapFloor = true; // if true, lowest point of model sits at y=0
        float padding = 0.0f; // optional inset margin (MC units each side)
        Config() = default;
    };

    // Config must be passed explicitly (MinGW limitation with nested struct defaults)
    explicit Normalizer(Config cfg);

    // Returns a new Mesh with all vertex positions transformed to MC space.
    // Does not modify normals (they stay as-is after uniform scale).
    [[nodiscard]] Mesh normalize(const Mesh &input) const;

private:
    Config cfg_;
};
