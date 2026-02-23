// TextureAtlas.cpp owns the stb_image_write implementation.
// GltfLoader.cpp defines TINYGLTF_NO_STB_IMAGE_WRITE so there is no conflict.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "core/TextureAtlas.hpp"
#include <stdexcept>
#include <cmath>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// How the atlas maps to Minecraft UVs
// ─────────────────────────────────────────────────────────────────────────────
//
//  Atlas is S×S pixels (S = power-of-2, min 16).
//  Color i lives at pixel  (px, py) = (i % S, i / S).
//
//  Minecraft UV space is [0,16] on both axes = full texture.
//  For an S×S texture: 1 UV unit = S/16 pixels.
//    → pixel px  corresponds to UV  px * (16 / S)
//
//  S=16:  1 UV unit = 1 px   →  UV [0,0,1,1], [1,0,2,1] … (clean integers)
//  S=32:  1 UV unit = 2 px   →  UV [0,0,0.5,0.5], [0.5,0,1.0,0.5] …
//
// ─────────────────────────────────────────────────────────────────────────────
// Why all pixels must be FULLY OPAQUE (no alpha = 0 anywhere)
// ─────────────────────────────────────────────────────────────────────────────
//
//  Minecraft's item renderer runs on OpenGL / Direct3D hardware.
//  Even when the sampler is set to "nearest", the GPU can sample at texel
//  boundaries when UV coords land exactly on a pixel edge (a floating-point
//  precision issue). A transparent (alpha=0) neighbor texel bleeds in and
//  makes the face appear semi-transparent or completely invisible.
//
//  Every vanilla and modded Minecraft texture avoids this by being a fully
//  opaque image with no empty / transparent regions. We do the same:
//    • Occupied slots → written with their color, alpha = 255.
//    • Unused slots   → filled with solid WHITE (255,255,255), alpha = 255.
//
//  The PNG therefore looks like a real painted texture — white background
//  with colored pixels at the occupied positions — not a mostly-empty
//  transparent image.
// ─────────────────────────────────────────────────────────────────────────────

// ── Helpers ───────────────────────────────────────────────────────────────────

int TextureAtlas::computeAtlasSize(int n) {
    int S = 16;
    while (S * S < n)
        S *= 2;
    return S;
}

uint32_t TextureAtlas::packColor(const glm::vec3 &c) {
    uint8_t r = static_cast<uint8_t>(std::round(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    uint8_t g = static_cast<uint8_t>(std::round(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    uint8_t b = static_cast<uint8_t>(std::round(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) <<  8) |
            static_cast<uint32_t>(b);
}

TextureAtlas::UVRect TextureAtlas::buildUV(int col) const {
    int S     = computeAtlasSize(static_cast<int>(colors_.size()));
    int px    = col % S;
    int py    = col / S;
    float sc  = 16.0f / static_cast<float>(S); // UV units per pixel
    return {
        static_cast<float>(px)     * sc,
        static_cast<float>(py)     * sc,
        static_cast<float>(px + 1) * sc,
        static_cast<float>(py + 1) * sc
    };
}

// ── Public interface ──────────────────────────────────────────────────────────

TextureAtlas::UVRect TextureAtlas::addColor(const glm::vec3 &color) {
    uint32_t key = packColor(color);
    auto it = colorToIndex_.find(key);
    if (it != colorToIndex_.end())
        return buildUV(it->second);

    int col = static_cast<int>(colors_.size());
    colors_.push_back(color);
    colorToIndex_[key] = col;
    return buildUV(col); // provisional — caller re-queries via recomputeUV()
}

TextureAtlas::UVRect TextureAtlas::recomputeUV(int col) const {
    return buildUV(col);
}

int TextureAtlas::getColorIndex(const glm::vec3 &color) const {
    auto it = colorToIndex_.find(packColor(color));
    return (it != colorToIndex_.end()) ? it->second : -1;
}

// ── PNG output ────────────────────────────────────────────────────────────────

void TextureAtlas::writePng(const std::string &path) const {
    if (colors_.empty()) {
        std::cerr << "[TextureAtlas] Warning: no colors to write.\n";
        return;
    }

    const int S        = atlasSize();
    const int N        = colorCount();
    constexpr int CH   = 4; // RGBA

    // ── Initialize every pixel to solid white (fully opaque) ─────────────────
    //
    // This is the critical step. Do NOT leave unused pixels as alpha=0.
    // White is the most neutral fallback: if a UV ever accidentally lands
    // on an unused slot it will appear white rather than invisible.
    std::vector<uint8_t> pixels(static_cast<size_t>(S * S * CH), 0);
    for (int i = 0; i < S * S * CH; i += CH) {
        pixels[i + 0] = 255; // R
        pixels[i + 1] = 255; // G
        pixels[i + 2] = 255; // B
        pixels[i + 3] = 255; // A — FULLY OPAQUE
    }

    // ── Write each registered color into its pixel slot ───────────────────────
    for (int i = 0; i < N; i++) {
        int px   = i % S;
        int py   = i / S;
        int base = (py * S + px) * CH;

        const glm::vec3 &c = colors_[i];
        pixels[base + 0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[base + 1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[base + 2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        pixels[base + 3] = 255; // fully opaque
    }

    if (!stbi_write_png(path.c_str(), S, S, CH, pixels.data(), S * CH))
        throw std::runtime_error("TextureAtlas: failed to write PNG to " + path);

    std::cout << "[TextureAtlas] Wrote " << N << " color(s) into "
              << S << "x" << S << " opaque RGBA atlas: " << path << "\n";
}