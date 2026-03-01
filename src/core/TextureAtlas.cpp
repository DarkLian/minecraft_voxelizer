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

TextureAtlas::TextureAtlas(int maxRowWidth, int maxAtlasSize)
    : maxRowWidth_(maxRowWidth)
    , maxAtlasSize_(std::max(1, maxAtlasSize))
{
    // 0 means auto — will be set in hintTotalPixels() or finalize()
    if (maxRowWidth_ <= 0)
        maxRowWidth_ = maxAtlasSize_; // fallback: full width, use hint to refine
}

void TextureAtlas::hintTotalPixels(int totalPixels) {
    if (rowWidthLocked_ || finalized_) return;
    // Choose row width ≈ sqrt(totalPixels), rounded to next pow2,
    // clamped to [64, maxAtlasSize_].
    if (totalPixels > 0) {
        int sqrtPx = static_cast<int>(std::sqrt(static_cast<double>(totalPixels)));
        maxRowWidth_ = std::clamp(nextPow2(sqrtPx), 64, maxAtlasSize_);
    }
    rowWidthLocked_ = true;
}

// ── Phase 1: Allocate ─────────────────────────────────────────────────────────

TextureAtlas::Region TextureAtlas::allocate(int w, int h) {
    if (finalized_)
        throw std::logic_error("TextureAtlas: allocate() called after finalize().");
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    // Wrap to a new row if this region doesn't fit.
    if (curX_ + w > maxRowWidth_ && curX_ > 0) {
        curY_ += rowH_;
        curX_  = 0;
        rowH_  = 0;
    }

    Region r{ curX_, curY_, w, h };

    curX_    += w;
    rowH_     = std::max(rowH_, h);
    packedW_  = std::max(packedW_, curX_);
    packedH_  = curY_ + rowH_;

    return r;
}

// ── Phase 2: Finalize ─────────────────────────────────────────────────────────

void TextureAtlas::finalize() {
    if (finalized_) return;

    if (packedW_ == 0 || packedH_ == 0) {
        atlasW_ = atlasH_ = 1;
    } else {
        atlasW_ = nextPow2(packedW_);
        atlasH_ = nextPow2(packedH_);

        // Safety: if either axis exceeds the size cap, the content would be
        // clipped which corrupts textures. Instead, report how bad it is and
        // let the user reduce --density. Never silently clip.
        if (atlasW_ > maxAtlasSize_ || atlasH_ > maxAtlasSize_) {
            std::cerr << "[TextureAtlas] WARNING: atlas is " << atlasW_
                      << " x " << atlasH_ << " px — content won't be clipped,\n"
                      << "  but this will be slow to generate and large on disk.\n"
                      << "  Reduce --density to shrink it. Recommended density for\n"
                      << "  this quality: source_texture_size / grid_resolution.\n";
        }
    }

    pixels_.assign(static_cast<size_t>(atlasW_) * atlasH_, glm::vec3(0.0f));
    finalized_ = true;

    std::cout << "[TextureAtlas] Layout finalised: " << atlasW_ << " x " << atlasH_
              << " px  (packed content: " << packedW_ << " x " << packedH_ << " px)\n";
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
    float u1 = (static_cast<float>(r.x)       / fw) * 16.0f;
    float v1 = (static_cast<float>(r.y)       / fh) * 16.0f;
    float u2 = (static_cast<float>(r.x + r.w) / fw) * 16.0f;
    float v2 = (static_cast<float>(r.y + r.h) / fh) * 16.0f;
    return {u1, v1, u2, v2};
}

// ── Write PNG ─────────────────────────────────────────────────────────────────

void TextureAtlas::writePng(const std::string &path) const {
    if (!finalized_)
        throw std::logic_error("TextureAtlas: writePng() called before finalize().");

    int w = atlasW_, h = atlasH_;
    int totalPx = w * h;
    std::vector<uint8_t> raw(static_cast<size_t>(totalPx) * 3);

    // Convert float [0,1] → uint8 in one tight loop.
    // Multiply by 255 + 0.5 to round rather than truncate.
    const glm::vec3 *src = pixels_.data();
    uint8_t         *dst = raw.data();
    for (int i = 0; i < totalPx; i++, src++, dst += 3) {
        dst[0] = static_cast<uint8_t>(glm::clamp(src->r, 0.0f, 1.0f) * 255.0f + 0.5f);
        dst[1] = static_cast<uint8_t>(glm::clamp(src->g, 0.0f, 1.0f) * 255.0f + 0.5f);
        dst[2] = static_cast<uint8_t>(glm::clamp(src->b, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    // Level 0 = no compression (just store). For atlas textures this is fine —
    // MC loads the PNG once at startup, disk size difference is irrelevant.
    // Gives maximum write speed (~10x faster than default level 8).
    stbi_write_png_compression_level = 0;

    int result = stbi_write_png(path.c_str(), w, h, 3, raw.data(), w * 3);
    if (!result)
        throw std::runtime_error("TextureAtlas: failed to write PNG to " + path);

    long long bytes = static_cast<long long>(w) * h * 3;
    std::cout << "[TextureAtlas] Wrote atlas (" << w << " x " << h
              << " px, ~" << (bytes / 1024 / 1024) << " MB uncompressed) to: "
              << path << "\n";
}