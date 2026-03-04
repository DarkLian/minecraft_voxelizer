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
// samplePixel — UNCHANGED from v1.x.
//
// Quad.sweepLayer, uAxis, vAxis, uStart, vStart carry identical semantics
// to v1.x regardless of whether the owning box spans 1 or N voxels in the
// sweep direction. The surface voxel for this face is always at sweepLayer,
// and u/v walk the face rectangle in the same way.
// ─────────────────────────────────────────────────────────────────────────────
glm::vec3 McModel::samplePixel(const GreedyMesher::Quad &q,
                               int pu, int pv,
                               int density,
                               const VoxelGrid &grid,
                               const Mesh &srcMesh) {
    int vu = q.uStart + pu / density;
    int vv = q.vStart + pv / density;
    int vs = q.sweepLayer;

    glm::ivec3 voxel;
    voxel[q.sweepAxis] = vs;
    voxel[q.uAxis]     = vu;
    voxel[q.vAxis]     = vv;

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

            glm::vec3 v0 = p1 - p0, v1 = p2 - p0, v2 = pixelPos - p0;
            float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1);
            float d11 = glm::dot(v1, v1), d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
            float denom = d00 * d11 - d01 * d01;

            glm::vec2 bary2(1.0f / 3.0f);
            if (std::abs(denom) > 1e-10f) {
                float bv = (d11 * d20 - d01 * d21) / denom;
                float bw = (d00 * d21 - d01 * d20) / denom;
                bary2 = glm::vec2(glm::clamp(bv, 0.0f, 1.0f),
                                  glm::clamp(bw, 0.0f, 1.0f));
                if (bary2.x + bary2.y > 1.0f)
                    bary2 /= bary2.x + bary2.y;
            }
            return srcMesh.sampleColor(tri, bary2);
        }
    }

    return grid.getColor(voxel.x, voxel.y, voxel.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// AabbKey — hash key over the six float bit-patterns of from/to.
// Safe because all values are exact integer multiples of (16/res).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct AabbKey {
    uint32_t fx, fy, fz, tx, ty, tz;
    bool operator==(const AabbKey &o) const {
        return fx==o.fx && fy==o.fy && fz==o.fz &&
               tx==o.tx && ty==o.ty && tz==o.tz;
    }
};
struct AabbKeyHash {
    size_t operator()(const AabbKey &k) const noexcept {
        size_t h = 14695981039346656037ULL;
        auto mix = [&](uint32_t v){ h ^= v; h *= 1099511628211ULL; };
        mix(k.fx); mix(k.fy); mix(k.fz);
        mix(k.tx); mix(k.ty); mix(k.tz);
        return h;
    }
};
AabbKey makeKey(const glm::vec3 &f, const glm::vec3 &t) {
    AabbKey k{};
    std::memcpy(&k.fx, &f.x, 4); std::memcpy(&k.fy, &f.y, 4); std::memcpy(&k.fz, &f.z, 4);
    std::memcpy(&k.tx, &t.x, 4); std::memcpy(&k.ty, &t.y, 4); std::memcpy(&k.tz, &t.z, 4);
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

    // ── Atlas pixel hint ──────────────────────────────────────────────────────
    {
        long long totalPx = 0;
        for (const auto &q : quads)
            totalPx += static_cast<long long>(std::max(1, q.uCount * density))
                     * std::max(1, q.vCount * density);
        atlas.hintTotalPixels(static_cast<int>(std::min(totalPx, static_cast<long long>(INT_MAX))));
    }

    // ── Phase 1: allocate atlas regions (one per quad) ────────────────────────
    std::vector<TextureAtlas::Region> regions;
    regions.reserve(quads.size());
    for (const auto &q : quads)
        regions.push_back(atlas.allocate(std::max(1, q.uCount * density),
                                         std::max(1, q.vCount * density)));

    // ── Phase 2: finalise atlas layout ────────────────────────────────────────
    atlas.finalize();

    // ── Phase 3: bake pixels + build intermediate elements (1 per quad) ───────
    std::vector<McElement> intermediate(quads.size());

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
    for (int i = 0; i < static_cast<int>(quads.size()); i++) {
        const auto &q = quads[i];
        const auto &r = regions[i];

        for (int pv = 0; pv < r.h; pv++)
            for (int pu = 0; pu < r.w; pu++)
                atlas.setPixel(r.x + pu, r.y + pv,
                               samplePixel(q, pu, pv, density, grid, srcMesh));

        intermediate[i] = buildElement(q, atlas.regionUV(r));
    }

    // ── Phase 4: AABB packing — merge faces of the same 3-D box ──────────────
    //
    // With the v2.0 GreedyMesher, every Quad belonging to the same 3-D box
    // already has identical from/to (the full box extents). AABB packing
    // therefore trivially groups all faces of each box into one McElement
    // with no extra work beyond what was already done for the v1.x
    // multi-face optimization.
    //
    // For fully interior boxes (zero exposed faces), the GreedyMesher already
    // emits zero quads, so they never appear here. No wasted elements.
    std::unordered_map<AabbKey, int, AabbKeyHash> aabbIndex;
    aabbIndex.reserve(intermediate.size());

    for (auto &el : intermediate) {
        AabbKey key = makeKey(el.from, el.to);
        auto it = aabbIndex.find(key);
        if (it == aabbIndex.end()) {
            aabbIndex[key] = static_cast<int>(elements_.size());
            elements_.push_back(std::move(el));
        } else {
            McElement &target = elements_[it->second];
            for (auto &[name, face] : el.faces)
                target.faces[name] = std::move(face);
        }
    }

    std::cout << "[McModel] Element packing: " << intermediate.size()
              << " quads → " << elements_.size() << " elements  ("
              << (intermediate.size() - elements_.size()) << " saved, "
              << (100 * (intermediate.size() - elements_.size())
                  / std::max(static_cast<size_t>(1), intermediate.size())) << "% reduction)\n";
}

// ── buildElement ──────────────────────────────────────────────────────────────
// quad.from / quad.to are now the FULL 3-D box extents, so the McElement's
// bounding box correctly spans the entire box across all sweep layers —
// not just the 1-voxel-thick face slab as in v1.x.
McElement McModel::buildElement(const GreedyMesher::Quad &quad,
                                const TextureAtlas::UVRect &uv) {
    McElement el;
    el.from = quad.from;   // full box min corner
    el.to   = quad.to;     // full box max corner

    McFace face;
    face.uv      = uv;
    face.texture = "#0";
    el.faces[McConstants::faceName(quad.face)] = face;
    return el;
}

// ── Compact JSON serializer ───────────────────────────────────────────────────

static void appendFloat(std::string &out, double v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%.10g", v);
    out.append(buf, n);
}

static void writeCompactStr(std::string &out, const nlohmann::ordered_json &j, int indent = 0) {
    if (j.is_object()) {
        out += "{\n";
        const std::string tab(indent + 1, '\t');
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) out += ",\n";
            first = false;
            out += tab;
            out += '"'; out += it.key(); out += "\": ";
            writeCompactStr(out, it.value(), indent + 1);
        }
        out += '\n'; out.append(indent, '\t'); out += '}';
    } else if (j.is_array()) {
        bool allPrim = true;
        for (const auto &e : j) if (e.is_object() || e.is_array()) { allPrim = false; break; }
        if (allPrim) {
            out += '[';
            bool first = true;
            for (const auto &e : j) {
                if (!first) out += ", "; first = false;
                if      (e.is_number_float())   appendFloat(out, e.get<double>());
                else if (e.is_number_integer())  out += std::to_string(e.get<int64_t>());
                else if (e.is_string())         { out += '"'; out += e.get<std::string>(); out += '"'; }
                else if (e.is_boolean())         out += e.get<bool>() ? "true" : "false";
                else                             out += e.dump();
            }
            out += ']';
        } else {
            out += "[\n";
            const std::string tab(indent + 1, '\t');
            bool first = true;
            for (const auto &e : j) {
                if (!first) out += ",\n"; first = false;
                out += tab; writeCompactStr(out, e, indent + 1);
            }
            out += '\n'; out.append(indent, '\t'); out += ']';
        }
    } else if (j.is_number_float())   appendFloat(out, j.get<double>());
    else if (j.is_number_integer())    out += std::to_string(j.get<int64_t>());
    else if (j.is_string())           { out += '"'; out += j.get<std::string>(); out += '"'; }
    else if (j.is_boolean())           out += j.get<bool>() ? "true" : "false";
    else                               out += j.dump();
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

    ojson elementsArr = ojson::array();
    for (const auto &el : elements_) {
        ojson elem;
        elem["from"] = {el.from.x, el.from.y, el.from.z};
        elem["to"]   = {el.to.x,   el.to.y,   el.to.z};

        if (el.hasRotation) {
            elem["rotation"] = {
                {"angle",   el.rotation.angle},
                {"axis",    std::string(1, el.rotation.axis)},
                {"origin",  {el.rotation.origin.x, el.rotation.origin.y, el.rotation.origin.z}},
                {"rescale", el.rotation.rescale}
            };
        }

        ojson facesObj;
        for (const auto &[faceName, mcFace] : el.faces) {
            ojson fj;
            fj["uv"]      = {mcFace.uv[0], mcFace.uv[1], mcFace.uv[2], mcFace.uv[3]};
            fj["texture"] = mcFace.texture;
            if (mcFace.tintindex >= 0) fj["tintindex"] = mcFace.tintindex;
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
        std::cout << "[McModel] WARNING: " << count << " elements may cause in-game lag.\n";
    else if (count > McConstants::ELEMENT_WARN_THRESHOLD)
        std::cout << "[McModel] Note: " << count << " elements is high but should load fine.\n";
    else
        std::cout << "[McModel] Element count is within safe limits.\n";
}
