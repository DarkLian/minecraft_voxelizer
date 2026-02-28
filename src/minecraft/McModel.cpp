#include "minecraft/McModel.hpp"
#include "minecraft/McConstants.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

McModel::McModel(const std::string &texturePath)
    : texturePath_(texturePath) {}

// ─────────────────────────────────────────────────────────────────────────────
// samplePixel — the core of the texture baking system.
//
// Given a pixel at offset (pu, pv) within a quad's atlas region, this method:
//   1. Figures out which voxel owns this pixel.
//   2. Computes the exact 3D MC-space position of this pixel's centre on the
//      exposed face of that voxel.
//   3. Looks up the mesh triangle that was stored for this voxel.
//   4. Re-interpolates the triangle's UV coordinates at that 3D point.
//   5. Samples the original mesh texture at those UVs.
//
// This gives true sub-voxel colour detail — different pixels within the same
// voxel face sample different texels from the source texture.
// ─────────────────────────────────────────────────────────────────────────────
glm::vec3 McModel::samplePixel(const GreedyMesher::Quad &q,
                                int pu, int pv,
                                int density,
                                const VoxelGrid &grid,
                                const Mesh      &srcMesh) const {
    // ── Which voxel owns this pixel? ──────────────────────────────────────────
    int vu = q.uStart + pu / density;   // voxel coordinate on uAxis
    int vv = q.vStart + pv / density;   // voxel coordinate on vAxis
    int vs = q.sweepLayer;              // voxel coordinate on sweepAxis

    glm::ivec3 voxel;
    voxel[q.sweepAxis] = vs;
    voxel[q.uAxis]     = vu;
    voxel[q.vAxis]     = vv;

    // ── Try to bake from the stored triangle ──────────────────────────────────
    int triIdx = grid.getTriIndex(voxel.x, voxel.y, voxel.z);

    if (triIdx >= 0 && triIdx < static_cast<int>(srcMesh.triangles.size())) {
        const Mesh::Triangle &tri = srcMesh.triangles[triIdx];

        // Only bake if this material actually has a texture image.
        bool hasTex = (tri.materialIndex >= 0 &&
                       tri.materialIndex < static_cast<int>(srcMesh.materials.size()) &&
                       srcMesh.materials[tri.materialIndex].hasTexture());

        if (hasTex) {
            // ── Compute 3D MC position of this pixel's centre ─────────────────
            // Grid dimensions for each axis
            int dims[3] = { grid.resX, grid.resY, grid.resZ };
            float voxelSizeS = 16.0f / static_cast<float>(dims[q.sweepAxis]);
            float voxelSizeU = 16.0f / static_cast<float>(dims[q.uAxis]);
            float voxelSizeV = 16.0f / static_cast<float>(dims[q.vAxis]);

            // Sub-pixel fractional position within the voxel face: [0,1)
            // Centre the sample in each sub-pixel cell.
            float fu = (static_cast<float>(pu % density) + 0.5f) / static_cast<float>(density);
            float fv = (static_cast<float>(pv % density) + 0.5f) / static_cast<float>(density);

            glm::vec3 pixelPos(0.0f);
            // Sweep axis: use the outer face of the voxel (exposed face centre)
            pixelPos[q.sweepAxis] = (vs + 0.5f) * voxelSizeS;  // voxel centre in sweep
            // U and V axes: interpolate within the voxel face
            pixelPos[q.uAxis]     = (vu + fu) * voxelSizeU;
            pixelPos[q.vAxis]     = (vv + fv) * voxelSizeV;

            // ── Barycentric coords of pixelPos relative to the triangle ────────
            auto verts = srcMesh.getTriangleVertices(tri);
            const glm::vec3 &p0 = verts[0].position;
            const glm::vec3 &p1 = verts[1].position;
            const glm::vec3 &p2 = verts[2].position;

            // Cramér's rule (same as Voxelizer::barycentricCoords)
            glm::vec3 v0 = p1 - p0;
            glm::vec3 v1 = p2 - p0;
            glm::vec3 v2 = pixelPos - p0;

            float d00 = glm::dot(v0, v0);
            float d01 = glm::dot(v0, v1);
            float d11 = glm::dot(v1, v1);
            float d20 = glm::dot(v2, v0);
            float d21 = glm::dot(v2, v1);
            float denom = d00 * d11 - d01 * d01;

            glm::vec2 bary2(1.0f / 3.0f);  // fallback: centroid
            if (std::abs(denom) > 1e-10f) {
                float bv = (d11 * d20 - d01 * d21) / denom;
                float bw = (d00 * d21 - d01 * d20) / denom;
                bary2 = glm::vec2(glm::clamp(bv, 0.0f, 1.0f),
                                  glm::clamp(bw, 0.0f, 1.0f));
                // Clamp so u+v ≤ 1 (keep inside triangle)
                if (bary2.x + bary2.y > 1.0f) {
                    float s = bary2.x + bary2.y;
                    bary2 /= s;
                }
            }

            return srcMesh.sampleColor(tri, bary2);
        }
    }

    // ── Fallback: flat per-voxel colour ───────────────────────────────────────
    return grid.getColor(voxel.x, voxel.y, voxel.z);
}

// ── Build ─────────────────────────────────────────────────────────────────────

void McModel::build(const std::vector<GreedyMesher::Quad> &quads,
                    const VoxelGrid                       &grid,
                    const Mesh                            &srcMesh,
                    TextureAtlas                          &atlas,
                    int                                    density) {
    density = std::max(1, density);
    elements_.clear();
    elements_.reserve(quads.size());

    // Phase 1: allocate atlas regions
    std::vector<TextureAtlas::Region> regions;
    regions.reserve(quads.size());
    for (const auto &q : quads) {
        int pw = std::max(1, q.uCount * density);
        int ph = std::max(1, q.vCount * density);
        regions.push_back(atlas.allocate(pw, ph));
    }

    // Phase 2: finalise layout
    atlas.finalize();

    // Phase 3: bake pixels + build elements
    for (size_t i = 0; i < quads.size(); i++) {
        const auto &q = quads[i];
        const auto &r = regions[i];

        for (int pv = 0; pv < r.h; pv++) {
            for (int pu = 0; pu < r.w; pu++) {
                glm::vec3 color = samplePixel(q, pu, pv, density, grid, srcMesh);
                atlas.setPixel(r.x + pu, r.y + pv, color);
            }
        }

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

// ── JSON serialization ────────────────────────────────────────────────────────

void McModel::writeJson(const std::string &outputPath) const {
    json root;
    root["parent"]   = "minecraft:block/block";
    root["textures"] = { {"0", texturePath_}, {"particle", texturePath_} };

    json elementsArr = json::array();
    for (const auto &el : elements_) {
        json elem;
        elem["from"] = {el.from.x, el.from.y, el.from.z};
        elem["to"]   = {el.to.x,   el.to.y,   el.to.z};

        if (el.hasRotation) {
            elem["rotation"] = {
                {"angle",   el.rotation.angle},
                {"axis",    std::string(1, el.rotation.axis)},
                {"origin",  {el.rotation.origin.x,
                             el.rotation.origin.y,
                             el.rotation.origin.z}},
                {"rescale", el.rotation.rescale}
            };
        }

        json facesObj;
        for (const auto &[faceName, mcFace] : el.faces) {
            json fj;
            fj["uv"]      = {mcFace.uv[0], mcFace.uv[1],
                             mcFace.uv[2], mcFace.uv[3]};
            fj["texture"] = mcFace.texture;
            if (mcFace.tintindex >= 0) fj["tintindex"] = mcFace.tintindex;
            facesObj[faceName] = fj;
        }
        elem["faces"] = facesObj;
        elementsArr.push_back(elem);
    }
    root["elements"] = elementsArr;

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
        std::cout << "[McModel] WARNING: " << count
                  << " elements exceeds MC safe limit ("
                  << McConstants::ELEMENT_CRASH_THRESHOLD << ").\n";
    else if (count > McConstants::ELEMENT_WARN_THRESHOLD)
        std::cout << "[McModel] Note: " << count
                  << " elements may cause Blockbench to lag.\n";
    else
        std::cout << "[McModel] Element count is within safe limits.\n";
}