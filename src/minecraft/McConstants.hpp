#pragma once

#include "core/VoxelGrid.hpp"  // for Face enum
#include <string>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// McConstants — Minecraft model format constants.
// Centralizes magic numbers so the rest of the code stays clean.
// ─────────────────────────────────────────────────────────────────────────────
namespace McConstants {
    // Minecraft's model coordinate space
    constexpr float MC_UNIT_MAX = 16.0f;
    constexpr float MC_UNIT_MIN = 0.0f;

    // Hard limits (beyond these, vanilla MC/Blockbench will struggle)
    constexpr int ELEMENT_WARN_THRESHOLD = 500; // Blockbench starts to lag
    constexpr int ELEMENT_CRASH_THRESHOLD = 3000; // MC renderer risk zone

    // Allowed rotation angles (Minecraft only supports these per-element)
    constexpr std::array<float, 5> ALLOWED_ROTATIONS = {-45.0f, -22.5f, 0.0f, 22.5f, 45.0f};

    // UV space is always [0, 16] regardless of actual PNG dimensions
    constexpr float UV_MAX = 16.0f;

    // Face index → Minecraft face name string
    inline const std::string &faceName(Face f) {
        static const std::array<std::string, 6> names = {
            "down", "up", "north", "south", "west", "east"
        };
        return names[static_cast<int>(f)];
    }

    // Default display transforms (reasonable defaults; user can tweak in Blockbench)
    constexpr struct DisplayDefaults {
        // GUI view: slight tilt so model is visible as an item icon
        float guiRotX = 30.0f;
        float guiRotY = 225.0f;
        float guiRotZ = 0.0f;
        float guiScaleXY = 0.625f;

        // Ground drop
        float groundTransY = 3.0f;
        float groundScale = 0.25f;

        // Third-person right hand
        float tpRotX = 75.0f;
        float tpRotY = 45.0f;
        float tpTransY = 2.5f;
        float tpScale = 0.375f;

        // First-person right hand
        float fpRotY = 45.0f;
        float fpScale = 0.4f;
    } DISPLAY;
} // namespace McConstants