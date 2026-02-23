#pragma once

#include "minecraft/McElement.hpp"
#include "core/TextureAtlas.hpp"
#include "pipeline/GreedyMesher.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// McModel — assembles the complete Minecraft model from greedy mesher output,
// then serializes to JSON (model) and PNG (texture atlas).
//
// Usage:
//   McModel model("darkaddons:item/my_model");
//   model.build(quads, atlas);   ← converts quads to McElements with UVs
//   model.writeJson("out.json");
// ─────────────────────────────────────────────────────────────────────────────
class McModel {
public:
    explicit McModel(const std::string &texturePath);

    // Convert greedy-meshed quads into McElements using the texture atlas.
    // Call this once after all quads are ready.
    void build(const std::vector<GreedyMesher::Quad> &quads,
               TextureAtlas &atlas);

    // Serialize to Minecraft JSON model format.
    void writeJson(const std::string &outputPath) const;

    // ── Queries ───────────────────────────────────────────────────────────────
    int elementCount() const { return static_cast<int>(elements_.size()); }
    bool isEmpty() const { return elements_.empty(); }

    // Warn if element count is approaching problematic thresholds
    void printStats() const;

private:
    std::string texturePath_; // e.g. "darkaddons:item/sword"
    std::vector<McElement> elements_;

    // Build a single McElement from one greedy quad
    McElement buildElement(const GreedyMesher::Quad &quad,
                           const TextureAtlas::UVRect &uv) const;
};