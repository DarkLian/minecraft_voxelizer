#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas (v1.2) — 2-D strip-packing texture atlas.
//
// Usage pattern (three phases):
//   Phase 1 – allocate(w, h)  → reserve a region
//   Phase 2 – finalize()      → lock layout, alloc pixel buffer
//   Phase 3 – setPixel()      → fill pixels
//   Phase 4 – regionUV() / writePng()
//
// Layout: horizontal strip packing, row wrap at maxRowWidth.
// Final dimensions: independent pow-of-2 per axis (NOT forced square),
//   each capped at maxAtlasSize (default 8192).
//
// Why non-square: a character model with density=8 at quality=7 might pack
// into ~3000×800 px.  Forcing square → 4096×4096 (4× larger than needed).
// Non-square → 4096×1024 (exactly what's needed, 4× smaller file).
//
// MC / GPU compatibility note: Minecraft loads item textures as standalone
// PNGs, not as part of the stitched atlas.  Non-square pow-of-2 textures
// (e.g. 4096×1024) are fully supported in MC 1.13+.
// ─────────────────────────────────────────────────────────────────────────────
class TextureAtlas {
public:
    using UVRect = std::array<float, 4>; // [u1, v1, u2, v2] in MC [0,16] space

    struct Region {
        int x, y, w, h;
    };

    // maxRowWidth  — pixel width before wrapping to a new row.
    //               Pass 0 to auto-select (square-root of estimated total area).
    // maxAtlasSize — hard cap on each axis (default 8192).
    explicit TextureAtlas(int maxRowWidth = 0, int maxAtlasSize = 8192);

    // ── Phase 1 ───────────────────────────────────────────────────────────────
    Region allocate(int w, int h);

    // ── Phase 2 ───────────────────────────────────────────────────────────────
    void finalize();

    // ── Phase 3 ───────────────────────────────────────────────────────────────
    void setPixel(int x, int y, const glm::vec3 &color);

    // ── Phase 4 ───────────────────────────────────────────────────────────────
    [[nodiscard]] UVRect regionUV(const Region &r) const;

    void writePng(const std::string &path) const;

    // ── Queries ───────────────────────────────────────────────────────────────
    [[nodiscard]] int atlasWidth() const { return atlasW_; }
    [[nodiscard]] int atlasHeight() const { return atlasH_; }
    [[nodiscard]] bool isFinalized() const { return finalized_; }

    // Call before finalize() to let the atlas choose optimal row width.
    // totalPixels = sum of (w*h) across all allocate() calls.
    void hintTotalPixels(int totalPixels);

private:
    static int nextPow2(int v);

    int maxRowWidth_;
    int maxAtlasSize_;
    bool rowWidthLocked_ = false;

    // Packing state (Phase 1)
    int curX_ = 0;
    int curY_ = 0;
    int rowH_ = 0;
    int packedW_ = 0;
    int packedH_ = 0;

    // Final atlas dimensions (set by finalize())
    int atlasW_ = 1;
    int atlasH_ = 1;
    bool finalized_ = false;

    std::vector<glm::vec3> pixels_;
};