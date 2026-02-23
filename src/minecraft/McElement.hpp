#pragma once

#include "core/VoxelGrid.hpp"  // Face enum
#include <glm/glm.hpp>
#include <array>
#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// McFace — one face of a Minecraft model element.
// Maps directly to a face entry in the JSON:
//   "up": { "uv": [u1,v1,u2,v2], "texture": "#0" }
// ─────────────────────────────────────────────────────────────────────────────
struct McFace {
    std::array<float, 4> uv      = {0.0f, 0.0f, 16.0f, 16.0f};
    std::string          texture = "#0";
    int                  tintindex = -1; // -1 means not set
};

// ─────────────────────────────────────────────────────────────────────────────
// McElement — one element ("cube") in a Minecraft model JSON.
// Stores from/to in MC units [0,16] and a map of face name → McFace.
// Only faces that should be rendered are included in the map.
// ─────────────────────────────────────────────────────────────────────────────
struct McElement {
    glm::vec3 from;
    glm::vec3 to;
    std::unordered_map<std::string, McFace> faces;

    // Optional rotation (MC allows max one rotation per element)
    struct Rotation {
        float     angle  = 0.0f;    // must be -45, -22.5, 0, 22.5, or 45
        char      axis   = 'y';     // 'x', 'y', or 'z'
        glm::vec3 origin = glm::vec3(8.0f); // pivot point in MC units
        bool      rescale = false;
    };
    // rotation is left unset (no rotation) unless explicitly needed
    bool hasRotation = false;
    Rotation rotation;
};
