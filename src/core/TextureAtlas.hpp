#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas — packs unique voxel colors into a square, power-of-2 PNG
// that matches Minecraft's standard texture conventions.
//
// Layout: row-major pixel grid, one pixel per unique color.
//
//   ≤  256 colors  →  16×16 PNG   (S = 16)
//   ≤ 1024 colors  →  32×32 PNG   (S = 32)
//   ≤ 4096 colors  →  64×64 PNG   (S = 64)
//
//   Color i  →  pixel (px, py) = (i % S, i / S)
//   UV rect  →  [px, py, px+1, py+1]  in Minecraft's [0,16] UV space
//
// Why integer UV coords?
//   For a 16×16 texture, Minecraft's UV [0,16] spans the full texture,
//   so 1 UV unit = 1 pixel. Every color slot gets clean integer UV values,
//   identical in style to hand-authored Blockbench models.
//
// Why fully opaque?
//   Minecraft's OpenGL renderer bilinearly bleeds neighboring texels into
//   each face sample. Any alpha=0 (transparent) pixel adjacent to a color
//   slot makes that face render semi-transparent or near-invisible.
//   All pixels in the output PNG are therefore fully opaque (alpha = 255);
//   unused slots are filled with solid white.
// ─────────────────────────────────────────────────────────────────────────────
class TextureAtlas {
public:
    using UVRect = std::array<float, 4>; // [u1, v1, u2, v2] in MC [0,16] space

    TextureAtlas() = default;

    // Register a color. Returns a provisional UV (atlas size not yet final).
    // Always call recomputeUV(index) after ALL addColor() calls.
    UVRect addColor(const glm::vec3 &color);

    // Final UV for a color index — call after ALL colors are registered.
    UVRect recomputeUV(int colorIndex) const;

    // Index of a previously registered color, or -1 if not found.
    int getColorIndex(const glm::vec3 &color) const;

    // Write the packed atlas PNG. Call after all colors have been registered.
    void writePng(const std::string &path) const;

    int colorCount() const { return static_cast<int>(colors_.size()); }
    int atlasSize()  const { return computeAtlasSize(colorCount()); }
    int atlasWidth() const { return atlasSize(); }
    int atlasHeight()const { return atlasSize(); }

private:
    // Smallest power-of-2 S ≥ 16 such that S*S ≥ n.
    static int computeAtlasSize(int n);
    static uint32_t packColor(const glm::vec3 &c);
    UVRect buildUV(int col) const;

    std::unordered_map<uint32_t, int> colorToIndex_;
    std::vector<glm::vec3>            colors_;
};