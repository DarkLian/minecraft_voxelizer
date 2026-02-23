#include "minecraft/McModel.hpp"
#include "minecraft/McConstants.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;

McModel::McModel(const std::string& texturePath)
    : texturePath_(texturePath)
{}

// ── Build ─────────────────────────────────────────────────────────────────────

void McModel::build(const std::vector<GreedyMesher::Quad>& quads,
                    TextureAtlas& atlas)
{
    elements_.clear();
    elements_.reserve(quads.size());

    // First pass: register all colors so the atlas knows its final width
    for (const auto& quad : quads)
        atlas.addColor(quad.color);

    // Second pass: build elements with correct final UVs
    for (const auto& quad : quads) {
        int colorIdx = atlas.getColorIndex(quad.color);
        TextureAtlas::UVRect uv = atlas.recomputeUV(colorIdx);
        elements_.push_back(buildElement(quad, uv));
    }
}

McElement McModel::buildElement(const GreedyMesher::Quad& quad,
                                const TextureAtlas::UVRect& uv) const
{
    McElement el;
    el.from = quad.from;
    el.to   = quad.to;

    // The quad only has one exposed face; add only that face to the element.
    // This is more efficient than adding all 6 and is valid MC JSON.
    McFace face;
    face.uv      = uv;
    face.texture = "#0";

    el.faces[McConstants::faceName(quad.face)] = face;
    return el;
}

// ── JSON Serialization ────────────────────────────────────────────────────────

void McModel::writeJson(const std::string& outputPath) const {
    json root;

    // Parent — use "block/block" for block-style items (has display transforms)
    root["parent"] = "minecraft:block/block";

    // Texture references
    root["textures"] = {
        {"0",        texturePath_},
        {"particle", texturePath_}
    };

    // Elements array
    json elementsArr = json::array();

    for (const auto& el : elements_) {
        json elem;

        elem["from"] = { el.from.x, el.from.y, el.from.z };
        elem["to"]   = { el.to.x,   el.to.y,   el.to.z   };

        // Rotation (only if set)
        if (el.hasRotation) {
            elem["rotation"] = {
                {"angle",   el.rotation.angle},
                {"axis",    std::string(1, el.rotation.axis)},
                {"origin",  { el.rotation.origin.x,
                              el.rotation.origin.y,
                              el.rotation.origin.z }},
                {"rescale", el.rotation.rescale}
            };
        }

        // Faces — only serialize faces that exist in the map
        json facesObj;
        for (const auto& [faceName, mcFace] : el.faces) {
            json faceJson;
            faceJson["uv"]      = { mcFace.uv[0], mcFace.uv[1],
                                    mcFace.uv[2], mcFace.uv[3] };
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
            {"rotation",    {McConstants::DISPLAY.guiRotX,
                             McConstants::DISPLAY.guiRotY,
                             McConstants::DISPLAY.guiRotZ}},
            {"scale",       {McConstants::DISPLAY.guiScaleXY,
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
                             McConstants::DISPLAY.tpRotY,
                             0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale}}
        }},
        {"thirdperson_lefthand", {
            {"rotation",    {McConstants::DISPLAY.tpRotX,
                             McConstants::DISPLAY.tpRotY,
                             0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale,
                             McConstants::DISPLAY.tpScale}}
        }},
        {"firstperson_righthand", {
            {"rotation",    {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.fpScale,
                             McConstants::DISPLAY.fpScale,
                             McConstants::DISPLAY.fpScale}}
        }},
        {"firstperson_lefthand", {
            {"rotation",    {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.fpScale,
                             McConstants::DISPLAY.fpScale,
                             McConstants::DISPLAY.fpScale}}
        }}
    };

    // Write to file (pretty-printed with 2-space indent)
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
                  << "). Consider reducing quality level.\n";
    else if (count > McConstants::ELEMENT_WARN_THRESHOLD)
        std::cout << "[McModel] ⚠ Note: " << count
                  << " elements may cause Blockbench to lag. "
                  << "Consider using --quality 1-2 for editing.\n";
    else
        std::cout << "[McModel] ✓ Element count is within safe limits.\n";
}
