#include "minecraft/McModel.hpp"
#include "minecraft/McConstants.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <climits>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <utility>
#include <unordered_map>
#ifdef _OPENMP
#endif

using json = nlohmann::json;

McModel::McModel(std::string texturePath)
    : texturePath_(std::move(texturePath)) {
}

// ─────────────────────────────────────────────────────────────────────────────
// samplePixel — the core of the texture baking system.
//
// Given a pixel at offset (pu, pv) within a quad's atlas region, this method:
//   1. Figures out which voxel owns this pixel.
//   2. Computes the exact 3D MC-space position of this pixel's center on the
//      exposed face of that voxel.
//   3. Looks up the mesh triangle that was stored for this voxel.
//   4. Re-interpolates the triangle's UV coordinates at that 3D point.
//   5. Samples the original mesh texture at those UVs.
//
// This gives true sub-voxel color detail — different pixels within the same
// voxel face sample different texels from the source texture.
// ─────────────────────────────────────────────────────────────────────────────
glm::vec3 McModel::samplePixel(const GreedyMesher::Quad &q,
                               int pu, int pv,
                               int density,
                               const VoxelGrid &grid,
                               const Mesh &srcMesh) {
    // ── Which voxel owns this pixel? ──────────────────────────────────────────
    int vu = q.uStart + pu / density;  // voxel coordinate on uAxis
    int vv = q.vStart + pv / density;  // voxel coordinate on vAxis
    int vs = q.sweepLayer;             // voxel coordinate on sweepAxis

    glm::ivec3 voxel;
    voxel[q.sweepAxis] = vs;
    voxel[q.uAxis]     = vu;
    voxel[q.vAxis]     = vv;

    // ── Try to bake from the stored triangle ──────────────────────────────────
    int triIdx = grid.getTriIndex(voxel.x, voxel.y, voxel.z, q.face);
    if (triIdx < 0)
        triIdx = grid.getAnyTriIndex(voxel.x, voxel.y, voxel.z);

    if (triIdx >= 0 && triIdx < static_cast<int>(srcMesh.triangles.size())) {
        const Mesh::Triangle &tri = srcMesh.triangles[triIdx];

        bool hasTex = (tri.materialIndex >= 0 &&
                       tri.materialIndex < static_cast<int>(srcMesh.materials.size()) &&
                       srcMesh.materials[tri.materialIndex].hasTexture());

        if (hasTex) {
            int dims[3] = {grid.resX, grid.resY, grid.resZ};
            float voxelSizeS = 16.0f / static_cast<float>(dims[q.sweepAxis]);
            float voxelSizeU = 16.0f / static_cast<float>(dims[q.uAxis]);
            float voxelSizeV = 16.0f / static_cast<float>(dims[q.vAxis]);

            float fu = (static_cast<float>(pu % density) + 0.5f) / static_cast<float>(density);
            float fv = (static_cast<float>(pv % density) + 0.5f) / static_cast<float>(density);

            glm::vec3 pixelPos(0.0f);
            pixelPos[q.sweepAxis] = (vs + 0.5f) * voxelSizeS;
            pixelPos[q.uAxis]     = (vu + fu) * voxelSizeU;
            pixelPos[q.vAxis]     = (vv + fv) * voxelSizeV;

            auto verts = srcMesh.getTriangleVertices(tri);
            const glm::vec3 &p0 = verts[0].position;
            const glm::vec3 &p1 = verts[1].position;
            const glm::vec3 &p2 = verts[2].position;

            glm::vec3 v0 = p1 - p0;
            glm::vec3 v1 = p2 - p0;
            glm::vec3 v2 = pixelPos - p0;

            float d00 = glm::dot(v0, v0);
            float d01 = glm::dot(v0, v1);
            float d11 = glm::dot(v1, v1);
            float d20 = glm::dot(v2, v0);
            float d21 = glm::dot(v2, v1);
            float denom = d00 * d11 - d01 * d01;

            glm::vec2 bary2(1.0f / 3.0f);
            if (std::abs(denom) > 1e-10f) {
                float bv = (d11 * d20 - d01 * d21) / denom;
                float bw = (d00 * d21 - d01 * d20) / denom;
                bary2 = glm::vec2(glm::clamp(bv, 0.0f, 1.0f),
                                  glm::clamp(bw, 0.0f, 1.0f));
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

// ─────────────────────────────────────────────────────────────────────────────
// AabbKey — 6-float bounding box used as a hash map key for element merging.
//
// All from/to values are exact multiples of (16.0 / res), computed from
// integer voxel indices, so identical AABBs always produce bit-for-bit
// identical float values — safe to memcpy-hash without epsilon tolerance.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct AabbKey {
    uint32_t fx, fy, fz;  // from.x/y/z bits
    uint32_t tx, ty, tz;  // to.x/y/z bits

    bool operator==(const AabbKey &o) const {
        return fx == o.fx && fy == o.fy && fz == o.fz &&
               tx == o.tx && ty == o.ty && tz == o.tz;
    }
};

struct AabbKeyHash {
    size_t operator()(const AabbKey &k) const noexcept {
        // FNV-1a style mix over the 6 uint32 words
        size_t h = 14695981039346656037ULL;
        auto mix = [&](uint32_t v) {
            h ^= static_cast<size_t>(v);
            h *= 1099511628211ULL;
        };
        mix(k.fx); mix(k.fy); mix(k.fz);
        mix(k.tx); mix(k.ty); mix(k.tz);
        return h;
    }
};

AabbKey makeKey(const glm::vec3 &from, const glm::vec3 &to) {
    AabbKey k{};
    std::memcpy(&k.fx, &from.x, 4);
    std::memcpy(&k.fy, &from.y, 4);
    std::memcpy(&k.fz, &from.z, 4);
    std::memcpy(&k.tx, &to.x,   4);
    std::memcpy(&k.ty, &to.y,   4);
    std::memcpy(&k.tz, &to.z,   4);
    return k;
}

} // anonymous namespace

// ── Build ─────────────────────────────────────────────────────────────────────

void McModel::build(const std::vector<GreedyMesher::Quad> &quads,
                    const VoxelGrid &grid,
                    const Mesh &srcMesh,
                    TextureAtlas &atlas,
                    int density) {
    density = std::max(1, density);
    elements_.clear();
    elements_.reserve(quads.size());

    // Give the atlas a total-pixel hint so it can choose an optimal row width
    // before any allocations happen.
    {
        long long totalPx = 0;
        for (const auto &q : quads)
            totalPx += static_cast<long long>(std::max(1, q.uCount * density))
                     * std::max(1, q.vCount * density);
        atlas.hintTotalPixels(static_cast<int>(std::min(totalPx, static_cast<long long>(INT_MAX))));
    }

    // Phase 1: allocate atlas regions (one per quad — unchanged)
    std::vector<TextureAtlas::Region> regions;
    regions.reserve(quads.size());
    for (const auto &q : quads) {
        int pw = std::max(1, q.uCount * density);
        int ph = std::max(1, q.vCount * density);
        regions.push_back(atlas.allocate(pw, ph));
    }

    // Phase 2: finalize atlas layout
    atlas.finalize();

    // Phase 3: bake pixels + build one intermediate McElement per quad.
    // We keep 1:1 indexing here so the OpenMP loop is race-free.
    std::vector<McElement> intermediate(quads.size());

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
    for (int i = 0; i < static_cast<int>(quads.size()); i++) {
        const auto &q = quads[i];
        const auto &r = regions[i];

        for (int pv = 0; pv < r.h; pv++) {
            for (int pu = 0; pu < r.w; pu++) {
                glm::vec3 color = samplePixel(q, pu, pv, density, grid, srcMesh);
                atlas.setPixel(r.x + pu, r.y + pv, color);
            }
        }

        TextureAtlas::UVRect uv = atlas.regionUV(r);
        intermediate[i] = buildElement(q, uv);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Phase 4: AABB-based multi-face element packing (OPTIMIZATION v1.3)
    //
    // A Minecraft model element supports up to 6 faces in its "faces" map,
    // but buildElement (Phase 3) always emits exactly 1 face per element.
    // Quads from different face directions that share an identical from/to
    // bounding box can be merged into one element with multiple face entries.
    //
    // Example: an isolated voxel with all 6 faces exposed currently produces
    // 6 single-face elements. After merging it becomes 1 element with 6 faces.
    //
    // Visual correctness: each face in the merged element retains its own
    // atlas UV rect (baked in Phase 3), so colors are completely unchanged.
    // The Minecraft renderer reads each face's UV independently.
    //
    // This is most impactful for thin/protruding geometry (hair, fingers,
    // clothing edges) where most surface voxels have 4–6 exposed faces.
    // ─────────────────────────────────────────────────────────────────────────

    // Maps AABB → insertion index in elements_
    std::unordered_map<AabbKey, int, AabbKeyHash> aabbIndex;
    aabbIndex.reserve(intermediate.size());

    for (auto &el : intermediate) {
        AabbKey key = makeKey(el.from, el.to);

        auto it = aabbIndex.find(key);
        if (it == aabbIndex.end()) {
            // First time we see this AABB — start a new merged element
            aabbIndex[key] = static_cast<int>(elements_.size());
            elements_.push_back(std::move(el));
        } else {
            // Same AABB already exists — merge this element's faces into it.
            // Each face name ("up", "north", …) is unique within one AABB
            // because the GreedyMesher emits at most one quad per face
            // direction per sweep layer.
            McElement &target = elements_[it->second];
            for (auto &[name, face] : el.faces)
                target.faces[name] = std::move(face);
        }
    }

    int before = static_cast<int>(intermediate.size());
    int after  = static_cast<int>(elements_.size());
    std::cout << "[McModel] Element packing: " << before << " quads → "
              << after << " elements  ("
              << (before - after) << " saved, "
              << (100 * (before - after) / std::max(1, before)) << "% reduction)\n";
}

McElement McModel::buildElement(const GreedyMesher::Quad &quad,
                                const TextureAtlas::UVRect &uv) {
    McElement el;
    el.from = quad.from;
    el.to   = quad.to;

    McFace face;
    face.uv      = uv;
    face.texture = "#0";
    el.faces[McConstants::faceName(quad.face)] = face;
    return el;
}

// ── Compact JSON serializer ───────────────────────────────────────────────────
// Writes objects/arrays-of-objects with indentation, primitive arrays inline.
// Uses snprintf for numbers to avoid per-number std::string heap allocations.

static void appendFloat(std::string &out, double v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%.10g", v);
    out.append(buf, n);
}

static void writeCompactStr(std::string &out,
                            const nlohmann::ordered_json &j,
                            int indent = 0) {
    if (j.is_object()) {
        out += "{\n";
        const std::string tab1(indent + 1, '\t');
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) out += ",\n";
            first = false;
            out += tab1;
            out += '"';
            out += it.key();
            out += "\": ";
            writeCompactStr(out, it.value(), indent + 1);
        }
        out += '\n';
        out.append(indent, '\t');
        out += '}';
    } else if (j.is_array()) {
        bool allPrimitive = true;
        for (const auto &elem : j)
            if (elem.is_object() || elem.is_array()) { allPrimitive = false; break; }

        if (allPrimitive) {
            out += '[';
            bool first = true;
            for (const auto &elem : j) {
                if (!first) out += ", ";
                first = false;
                if (elem.is_number_float())
                    appendFloat(out, elem.get<double>());
                else if (elem.is_number_integer())
                    out += std::to_string(elem.get<int64_t>());
                else if (elem.is_string()) {
                    out += '"';
                    out += elem.get<std::string>();
                    out += '"';
                } else if (elem.is_boolean())
                    out += elem.get<bool>() ? "true" : "false";
                else
                    out += elem.dump();
            }
            out += ']';
        } else {
            out += "[\n";
            const std::string tab1(indent + 1, '\t');
            bool first = true;
            for (const auto &elem : j) {
                if (!first) out += ",\n";
                first = false;
                out += tab1;
                writeCompactStr(out, elem, indent + 1);
            }
            out += '\n';
            out.append(indent, '\t');
            out += ']';
        }
    } else if (j.is_number_float()) {
        appendFloat(out, j.get<double>());
    } else if (j.is_number_integer()) {
        out += std::to_string(j.get<int64_t>());
    } else if (j.is_string()) {
        out += '"';
        out += j.get<std::string>();
        out += '"';
    } else if (j.is_boolean()) {
        out += j.get<bool>() ? "true" : "false";
    } else {
        out += j.dump();
    }
}

// ── JSON serialization ────────────────────────────────────────────────────────

void McModel::writeJson(const std::string &outputPath) const {
    using ojson = nlohmann::ordered_json;

    ojson root;

    root["textures"] = ojson::object();
    root["textures"]["0"] = texturePath_;

    root["display"] = {
        {"gui", {
            {"rotation", {McConstants::DISPLAY.guiRotX, McConstants::DISPLAY.guiRotY, McConstants::DISPLAY.guiRotZ}},
            {"scale",    {McConstants::DISPLAY.guiScaleXY, McConstants::DISPLAY.guiScaleXY, McConstants::DISPLAY.guiScaleXY}}
        }},
        {"ground", {
            {"translation", {0.0f, McConstants::DISPLAY.groundTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.groundScale, McConstants::DISPLAY.groundScale, McConstants::DISPLAY.groundScale}}
        }},
        {"thirdperson_righthand", {
            {"rotation",    {McConstants::DISPLAY.tpRotX, McConstants::DISPLAY.tpRotY, 0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale, McConstants::DISPLAY.tpScale, McConstants::DISPLAY.tpScale}}
        }},
        {"thirdperson_lefthand", {
            {"rotation",    {McConstants::DISPLAY.tpRotX, McConstants::DISPLAY.tpRotY, 0.0f}},
            {"translation", {0.0f, McConstants::DISPLAY.tpTransY, 0.0f}},
            {"scale",       {McConstants::DISPLAY.tpScale, McConstants::DISPLAY.tpScale, McConstants::DISPLAY.tpScale}}
        }},
        {"firstperson_righthand", {
            {"rotation", {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",    {McConstants::DISPLAY.fpScale, McConstants::DISPLAY.fpScale, McConstants::DISPLAY.fpScale}}
        }},
        {"firstperson_lefthand", {
            {"rotation", {0.0f, McConstants::DISPLAY.fpRotY, 0.0f}},
            {"scale",    {McConstants::DISPLAY.fpScale, McConstants::DISPLAY.fpScale, McConstants::DISPLAY.fpScale}}
        }}
    };

    // ── Elements ──────────────────────────────────────────────────────────────
    ojson elementsArr = ojson::array();
    for (const auto &el : elements_) {
        ojson elem;
        elem["from"] = {el.from.x, el.from.y, el.from.z};
        elem["to"]   = {el.to.x,   el.to.y,   el.to.z};

        if (el.hasRotation) {
            elem["rotation"] = {
                {"angle",  el.rotation.angle},
                {"axis",   std::string(1, el.rotation.axis)},
                {"origin", {el.rotation.origin.x, el.rotation.origin.y, el.rotation.origin.z}},
                {"rescale", el.rotation.rescale}
            };
        }

        ojson facesObj;
        for (const auto &[faceName, mcFace] : el.faces) {
            ojson fj;
            fj["uv"]      = {mcFace.uv[0], mcFace.uv[1], mcFace.uv[2], mcFace.uv[3]};
            fj["texture"] = mcFace.texture;
            if (mcFace.tintindex >= 0)
                fj["tintindex"] = mcFace.tintindex;
            facesObj[faceName] = fj;
        }
        elem["faces"] = facesObj;
        elementsArr.push_back(elem);
    }
    root["elements"] = elementsArr;

    std::ofstream out(outputPath);
    if (!out)
        throw std::runtime_error("McModel: cannot open output file: " + outputPath);

    std::string buf;
    buf.reserve(elements_.size() * 150 + 4096);
    writeCompactStr(buf, root);
    buf += '\n';
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out.close();

    std::cout << "[McModel] Wrote " << elements_.size()
              << " elements to: " << outputPath << "\n";
}

void McModel::printStats() const {
    int count = elementCount();
    std::cout << "[McModel] MC elements (JSON cubes): " << count << "\n"
              << "          NOTE: This is NOT the voxel count. Elements are\n"
              << "          merged voxel faces — higher quality CAN produce\n"
              << "          fewer elements when large flat regions merge.\n";
    if (count > McConstants::ELEMENT_PERF_THRESHOLD)
        std::cout << "[McModel] WARNING: " << count << " elements is very high"
                  << " — may cause in-game lag. Consider reducing --quality or --density.\n";
    else if (count > McConstants::ELEMENT_WARN_THRESHOLD)
        std::cout << "[McModel] Note: " << count << " elements is high but should load fine.\n";
    else
        std::cout << "[McModel] Element count is within safe limits.\n";
}
