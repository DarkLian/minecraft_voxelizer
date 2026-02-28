#include "minecraft/McModel.hpp"
#include "minecraft/McConstants.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

McModel::McModel(const std::string &texturePath)
    : texturePath_(texturePath) {}

// ── Build ─────────────────────────────────────────────────────────────────────

void McModel::build(const std::vector<GreedyMesher::Quad> &quads,
                    const VoxelGrid                       &grid,
                    TextureAtlas                          &atlas,
                    int                                    density) {
    density = std::max(1, density);
    elements_.clear();
    elements_.reserve(quads.size());

    // ── Phase 1: Allocate one atlas region per quad ────────────────────────
    // Region size = (voxel extents × density) pixels.
    std::vector<TextureAtlas::Region> regions;
    regions.reserve(quads.size());

    for (const auto &q : quads) {
        int pw = std::max(1, q.uCount * density);
        int ph = std::max(1, q.vCount * density);
        regions.push_back(atlas.allocate(pw, ph));
    }

    // ── Phase 2: Finalise atlas layout (creates pixel buffer) ─────────────
    atlas.finalize();

    // ── Phase 3: Fill pixels + build elements ─────────────────────────────
    for (size_t i = 0; i < quads.size(); i++) {
        const auto &q  = quads[i];
        const auto &r  = regions[i];

        // Fill every pixel in this quad's atlas region by sampling the voxel
        // grid.  Integer division maps multiple pixels to the same voxel,
        // producing a nearest-neighbour "zoom" at density > 1.
        for (int pv = 0; pv < r.h; pv++) {
            for (int pu = 0; pu < r.w; pu++) {
                int vu = q.uStart + pu / density;
                int vv = q.vStart + pv / density;

                glm::ivec3 pos(0);
                pos[q.sweepAxis] = q.sweepLayer;
                pos[q.uAxis]     = vu;
                pos[q.vAxis]     = vv;

                glm::vec3 color = grid.getColor(pos.x, pos.y, pos.z);
                atlas.setPixel(r.x + pu, r.y + pv, color);
            }
        }

        // Build MC element with the correct UV into the finalised atlas.
        TextureAtlas::UVRect uv = atlas.regionUV(r);
        elements_.push_back(buildElement(q, uv));
    }
}

McElement McModel::buildElement(const GreedyMesher::Quad  &quad,
                                const TextureAtlas::UVRect &uv) const {
    McElement el;
    el.from = quad.from;
    el.to   = quad.to;

    McFace face;
    face.uv      = uv;
    face.texture = "#0";

    el.faces[McConstants::faceName(quad.face)] = face;
    return el;
}

// ── JSON Serialization ────────────────────────────────────────────────────────

void McModel::writeJson(const std::string &outputPath) const {
    json root;
    root["parent"] = "minecraft:block/block";
    root["textures"] = {
        {"0",        texturePath_},
        {"particle", texturePath_}
    };

    json elementsArr = json::array();
    for (const auto &el : elements_) {
        json elem;
        elem["from"] = {el.from.x, el.from.y, el.from.z};
        elem["to"]   = {el.to.x,   el.to.y,   el.to.z};

        if (el.hasRotation) {
            elem["rotation"] = {
                {"angle",  el.rotation.angle},
                {"axis",   std::string(1, el.rotation.axis)},
                {"origin", {el.rotation.origin.x,
                            el.rotation.origin.y,
                            el.rotation.origin.z}},
                {"rescale", el.rotation.rescale}
            };
        }

        json facesObj;
        for (const auto &[faceName, mcFace] : el.faces) {
            json faceJson;
            faceJson["uv"]      = {mcFace.uv[0], mcFace.uv[1],
                                   mcFace.uv[2], mcFace.uv[3]};
            faceJson["texture"] = mcFace.texture;
            if (mcFace.tintindex >= 0)
                faceJson["tintindex"] = mcFace.tintindex;
            facesObj[faceName] = faceJson;
        }
        elem["faces"] = facesObj;
        elementsArr.push_back(elem);
    }
    root["elements"] = elementsArr;

    // Display transforms
    root["display"] = {
        {"gui", {
            {"rotation", {McConstants::DISPLAY.guiRotX,
                          McConstants::DISPLAY.guiRotY,
                          McConstants::DISPLAY.guiRotZ}},
            {"scale",    {McConstants::DISPLAY.guiScaleXY,
                          McConstants::DISPLAY.guiScaleXY,
                          McConstants::DISPLAY.guiScaleXY}}
        }},
        {"ground", {
            {"translation", {0.0f, McConstants::DISPLAY.groundTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.groundScale,
                             McConstants::DISPLAY.groundScale,
                             McConstants::DISPLAY.groundScale}}
        }},
        {"thirdperson_righthand", {
            {"rotation",    {McConstants::DISPLAY.tpRotX,
                             McConstants::DISPLAY.tpRotY, 0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale}}
        }},
        {"thirdperson_lefthand", {
            {"rotation",    {McConstants::DISPLAY.tpRotX,
                             McConstants::DISPLAY.tpRotY, 0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale}}
        }},
        {"firstperson_righthand", {
            {"rotation", {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",    {McConstants::DISPLAY.fpScale,
                          McConstants::DISPLAY.fpScale,
                          McConstants::DISPLAY.fpScale}}
        }},
        {"firstperson_lefthand", {
            {"rotation", {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",    {McConstants::DISPLAY.fpScale,
                          McConstants::DISPLAY.fpScale,
                          McConstants::DISPLAY.fpScale}}
        }}
    };

    std::ofstream out(outputPath);
    if (!out)
        throw std::runtime_error("McModel: cannot open output file: " + outputPath);
    out << root.dump(2);
    out.close();

    std::cout << "[McModel] Wrote " << elements_.size()
              << " elements to: " << outputPath << "\n";
}

void McModel::printStats() const {
    int count = elementCount();
    std::cout << "[McModel] Element count: " << count << "\n";

    if (count > McConstants::ELEMENT_CRASH_THRESHOLD)
        std::cout << "[McModel] ⚠ WARNING: " << count
                  << " elements exceeds MC renderer safe limit ("
                  << McConstants::ELEMENT_CRASH_THRESHOLD
                  << "). Consider reducing --quality.\n";
    else if (count > McConstants::ELEMENT_WARN_THRESHOLD)
        std::cout << "[McModel] ⚠ Note: " << count
                  << " elements may cause Blockbench to lag. "
                  << "Consider using --quality 1-2 for editing.\n";
    else
        std::cout << "[McModel] ✓ Element count is within safe limits.\n";
}