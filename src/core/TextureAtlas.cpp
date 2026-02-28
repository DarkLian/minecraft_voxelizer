// TextureAtlas.cpp owns the stb_image_write implementation.
// GltfLoader.cpp defines TINYGLTF_NO_STB_IMAGE_WRITE so there is no conflict.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "core/TextureAtlas.hpp"
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────────

int TextureAtlas::nextPow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

// ── Constructor ───────────────────────────────────────────────────────────────

TextureAtlas::TextureAtlas(int maxRowWidth)
    : maxRowWidth_(std::max(1, maxRowWidth)) {}

// ── Phase 1: Allocate ─────────────────────────────────────────────────────────

TextureAtlas::Region TextureAtlas::allocate(int w, int h) {
    if (finalized_)
        throw std::logic_error("TextureAtlas: allocate() called after finalize().");
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    // If the new quad doesn't fit on the current row, start a new one.
    if (curX_ + w > maxRowWidth_ && curX_ > 0) {
        curY_  += rowH_;
        curX_   = 0;
        rowH_   = 0;
    }

    Region r{ curX_, curY_, w, h };

    curX_    += w;
    rowH_     = std::max(rowH_, h);
    packedW_  = std::max(packedW_, curX_);
    packedH_  = curY_ + rowH_;   // updated after every allocation

    return r;
}

// ── Phase 2: Finalize ─────────────────────────────────────────────────────────

void TextureAtlas::finalize() {
    if (finalized_) return;

    if (packedW_ == 0 || packedH_ == 0) {
        atlasW_ = atlasH_ = 1;
    } else {
        // Square atlas: take the larger of the two packed dimensions so both
        // width and height are the same power-of-2.  This gives maximum
        // compatibility with MC and GPU drivers that prefer square textures.
        int side = nextPow2(std::max(packedW_, packedH_));
        atlasW_  = side;
        atlasH_  = side;
    }

    pixels_.assign(static_cast<size_t>(atlasW_) * atlasH_, glm::vec3(0.0f));
    finalized_ = true;

    if (atlasW_ > 8192 || atlasH_ > 8192)
        std::cerr << "[TextureAtlas] WARNING: atlas is " << atlasW_ << "×" << atlasH_
                  << " px — consider using a lower --density value.\n";

    std::cout << "[TextureAtlas] Layout finalised: " << atlasW_ << " × " << atlasH_
              << " px (packed area " << packedW_ << " × " << packedH_ << " px)\n";
}

// ── Phase 3: Set pixels ───────────────────────────────────────────────────────

void TextureAtlas::setPixel(int x, int y, const glm::vec3 &color) {
    if (!finalized_)
        throw std::logic_error("TextureAtlas: setPixel() called before finalize().");
    if (x < 0 || x >= atlasW_ || y < 0 || y >= atlasH_) return;
    pixels_[x + y * atlasW_] = color;
}

// ── Phase 4: UV query ─────────────────────────────────────────────────────────

TextureAtlas::UVRect TextureAtlas::regionUV(const Region &r) const {
    if (!finalized_)
        throw std::logic_error("TextureAtlas: regionUV() called before finalize().");

    float fw = static_cast<float>(atlasW_);
    float fh = static_cast<float>(atlasH_);
    float u1 = (static_cast<float>(r.x)        / fw) * 16.0f;
    float v1 = (static_cast<float>(r.y)        / fh) * 16.0f;
    float u2 = (static_cast<float>(r.x + r.w)  / fw) * 16.0f;
    float v2 = (static_cast<float>(r.y + r.h)  / fh) * 16.0f;
    return {u1, v1, u2, v2};
}

// ── Write PNG ─────────────────────────────────────────────────────────────────

void TextureAtlas::writePng(const std::string &path) const {
    if (!finalized_)
        throw std::logic_error("TextureAtlas: writePng() called before finalize().");

    int w = atlasW_, h = atlasH_;
    std::vector<uint8_t> raw(static_cast<size_t>(w) * h * 3);

    for (int i = 0; i < w * h; i++) {
        const glm::vec3 &c = pixels_[i];
        raw[i * 3 + 0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        raw[i * 3 + 1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        raw[i * 3 + 2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    }

    int result = stbi_write_png(path.c_str(), w, h, 3, raw.data(), w * 3);
    if (!result)
        throw std::runtime_error("TextureAtlas: failed to write PNG to " + path);

    std::cout << "[TextureAtlas] Wrote atlas (" << w << "×" << h << " px) to: " << path << "\n";
}