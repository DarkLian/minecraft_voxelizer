#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas — collects unique voxel face colors during greedy meshing,
// packs them into a compact PNG, and returns UV coordinates in [0,16] space
// (Minecraft's UV convention regardless of actual pixel resolution).
//
// Layout: horizontal strip, one pixel per unique color.
//   e.g. 5 colors → 5×1 pixel PNG
//        UV for color[2] → [u=2/5*16, v=0, u2=3/5*16, v2=16]
// ─────────────────────────────────────────────────────────────────────────────
class TextureAtlas {
public:
    using UVRect = std::array<float, 4>; // [u1, v1, u2, v2] in MC [0,16] space

    TextureAtlas() = default;

    // Register a color. UVs are provisional until all colors are added.
    // Call recomputeUV(index) after all addColor() calls for final UVs.
    UVRect addColor(const glm::vec3& color);

    // Get final UV for a color index — call after all colors are registered.
    UVRect recomputeUV(int colorIndex) const;

    // Get index of a previously registered color, or -1.
    int getColorIndex(const glm::vec3& color) const;

    // Write the packed atlas PNG. Call after all colors have been registered.
    void writePng(const std::string& path) const;

    int colorCount()  const { return static_cast<int>(colors_.size()); }
    int atlasWidth()  const { return static_cast<int>(colors_.size()); }
    int atlasHeight() const { return 1; }

private:
    static uint32_t packColor(const glm::vec3& c);
    UVRect buildUV(int col) const;

    std::unordered_map<uint32_t, int> colorToIndex_;
    std::vector<glm::vec3>            colors_;
};
