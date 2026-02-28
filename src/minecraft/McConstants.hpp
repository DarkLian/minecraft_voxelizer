#pragma once

#include "core/VoxelGrid.hpp"
#include <string>
#include <array>

namespace McConstants {
    constexpr float MC_UNIT_MAX = 16.0f;
    constexpr float MC_UNIT_MIN = 0.0f;

    // Element count thresholds
    constexpr int ELEMENT_WARN_THRESHOLD  =  500; // Blockbench starts to lag
    constexpr int ELEMENT_CRASH_THRESHOLD = 3000; // MC renderer risk zone

    constexpr std::array<float, 5> ALLOWED_ROTATIONS = {-45.0f,-22.5f,0.0f,22.5f,45.0f};

    constexpr float UV_MAX = 16.0f;

    inline const std::string &faceName(Face f) {
        static const std::array<std::string, 6> names = {
            "down","up","north","south","west","east"
        };
        return names[static_cast<int>(f)];
    }

    constexpr struct DisplayDefaults {
        float guiRotX = 30.0f, guiRotY = 225.0f, guiRotZ = 0.0f;
        float guiScaleXY = 0.625f;
        float groundTransY = 3.0f, groundScale = 0.25f;
        float tpRotX = 75.0f, tpRotY = 45.0f, tpTransY = 2.5f, tpScale = 0.375f;
        float fpRotY = 45.0f, fpScale = 0.4f;
    } DISPLAY;
}