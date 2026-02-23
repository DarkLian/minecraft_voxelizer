// TextureAtlas.cpp owns the stb_image_write implementation.
// GltfLoader.cpp defines TINYGLTF_NO_STB_IMAGE_WRITE so there is no conflict.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "core/TextureAtlas.hpp"
#include <stdexcept>
#include <cmath>
#include <iostream>

// ── Private helper ────────────────────────────────────────────────────────────

uint32_t TextureAtlas::packColor(const glm::vec3 &c) {
    // Clamp and convert to 8-bit per channel, pack into uint32
    uint8_t r = static_cast<uint8_t>(std::round(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    uint8_t g = static_cast<uint8_t>(std::round(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    uint8_t b = static_cast<uint8_t>(std::round(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

// ── Public ────────────────────────────────────────────────────────────────────

TextureAtlas::UVRect TextureAtlas::addColor(const glm::vec3 &color) {
    uint32_t key = packColor(color);

    // Return existing UV if already registered
    auto it = colorToIndex_.find(key);
    if (it != colorToIndex_.end()) {
        int col = it->second;
        float u1 = (static_cast<float>(col) / static_cast<float>(colors_.size())) * 16.0f;
        float u2 = (static_cast<float>(col + 1) / static_cast<float>(colors_.size())) * 16.0f;
        // Note: recomputed below after all colors known — see writePng note
        (void) u1;
        (void) u2;
        // We store the index and recompute UVs at mesh time using colorCount()
        // so callers must call addColor before all colors are finalized.
        // Return placeholder — actual UV computation done in McModel::build()
        return buildUV(col);
    }

    int col = static_cast<int>(colors_.size());
    colors_.push_back(color);
    colorToIndex_[key] = col;
    return buildUV(col);
}

TextureAtlas::UVRect TextureAtlas::buildUV(int col) const {
    // UV must be computed relative to FINAL atlas width, but we don't know
    // the final count until all colors are added. We use current size+1 here;
    // the caller (McModel) re-queries after all colors are added via recomputeUV().
    int total = static_cast<int>(colors_.size());
    if (total == 0) total = 1;
    float u1 = (static_cast<float>(col) / static_cast<float>(total)) * 16.0f;
    float u2 = (static_cast<float>(col + 1) / static_cast<float>(total)) * 16.0f;
    return {u1, 0.0f, u2, 16.0f};
}

TextureAtlas::UVRect TextureAtlas::recomputeUV(int col) const {
    int total = static_cast<int>(colors_.size());
    if (total == 0) total = 1;
    float u1 = (static_cast<float>(col) / static_cast<float>(total)) * 16.0f;
    float u2 = (static_cast<float>(col + 1) / static_cast<float>(total)) * 16.0f;
    return {u1, 0.0f, u2, 16.0f};
}

int TextureAtlas::getColorIndex(const glm::vec3 &color) const {
    uint32_t key = packColor(color);
    auto it = colorToIndex_.find(key);
    if (it == colorToIndex_.end()) return -1;
    return it->second;
}

void TextureAtlas::writePng(const std::string &path) const {
    if (colors_.empty()) {
        std::cerr << "[TextureAtlas] Warning: no colors to write.\n";
        return;
    }

    int w = static_cast<int>(colors_.size());
    int h = 1;

    // Build raw RGB pixel data
    std::vector<uint8_t> pixels(w * h * 3);
    for (int i = 0; i < w; i++) {
        const glm::vec3 &c = colors_[i];
        pixels[i * 3 + 0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[i * 3 + 1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[i * 3 + 2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    }

    int result = stbi_write_png(path.c_str(), w, h, 3, pixels.data(), w * 3);
    if (!result)
        throw std::runtime_error("TextureAtlas: failed to write PNG to " + path);

    std::cout << "[TextureAtlas] Wrote " << w << " colors to: " << path << "\n";
}