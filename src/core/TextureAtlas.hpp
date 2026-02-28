#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas (v1.1) — 2-D strip-packing texture atlas.
//
// Usage pattern (three phases):
//
//   Phase 1 – Allocate regions (before colours are known):
//     Region r = atlas.allocate(pixelW, pixelH);
//
//   Phase 2 – Finalize layout (creates pixel buffer, must come after ALL
//             allocations and before any setPixel / regionUV calls):
//     atlas.finalize();
//
//   Phase 3 – Fill pixels:
//     atlas.setPixel(r.x + px, r.y + py, color);
//
//   Phase 4 – Query UVs and write PNG:
//     UVRect uv = atlas.regionUV(r);
//     atlas.writePng("out.png");
//
// Layout: horizontal strip packing with row wrap at maxRowWidth.
//   e.g. quads with sizes 4×2, 6×3, 8×2 → packed left-to-right, new row
//        when the next quad would exceed maxRowWidth.
//
// Atlas dimensions are rounded up to the next power of two so Minecraft and
// older GPU drivers are happy.  UV coordinates are in Minecraft's [0,16]
// space and always reference the power-of-two dimensions.
// ─────────────────────────────────────────────────────────────────────────────
class TextureAtlas {
public:
    using UVRect = std::array<float, 4>; // [u1, v1, u2, v2] in MC [0,16] space

    // A rectangular region allocated inside the atlas (pixel coordinates).
    struct Region { int x, y, w, h; };

    // maxRowWidth: maximum pixel width before starting a new row (default 4096).
    explicit TextureAtlas(int maxRowWidth = 4096);

    // ── Phase 1 ───────────────────────────────────────────────────────────────
    // Allocate a (w × h) pixel region.  Returns its position in the atlas.
    // Must be called before finalize().
    Region allocate(int w, int h);

    // ── Phase 2 ───────────────────────────────────────────────────────────────
    // Lock the layout, allocate the pixel buffer (filled with black).
    // Must be called after all allocate() calls and before setPixel() or
    // regionUV().  Calling allocate() after finalize() is a logic error.
    void finalize();

    // ── Phase 3 ───────────────────────────────────────────────────────────────
    // Set a single pixel (absolute atlas coordinates).
    // Must be called after finalize().
    void setPixel(int x, int y, const glm::vec3 &color);

    // ── Phase 4 ───────────────────────────────────────────────────────────────
    // Get the Minecraft UV rect [u1,v1,u2,v2] in [0,16] space for a region.
    // Must be called after finalize().
    UVRect regionUV(const Region &r) const;

    // Write the atlas PNG.  Must be called after finalize() and after all
    // setPixel() calls.
    void writePng(const std::string &path) const;

    // ── Queries ───────────────────────────────────────────────────────────────
    int atlasWidth()  const { return atlasW_; }
    int atlasHeight() const { return atlasH_; }
    bool isFinalized() const { return finalized_; }

private:
    static int nextPow2(int v);

    int maxRowWidth_;

    // Packing state (Phase 1)
    int curX_  = 0;
    int curY_  = 0;
    int rowH_  = 0;
    int packedW_ = 0; // widest point reached so far
    int packedH_ = 0; // total height accumulated

    // Final atlas dimensions (set by finalize())
    int atlasW_ = 1;
    int atlasH_ = 1;
    bool finalized_ = false;

    // Pixel buffer [atlasW_ × atlasH_], RGB in [0,1]
    std::vector<glm::vec3> pixels_;
};