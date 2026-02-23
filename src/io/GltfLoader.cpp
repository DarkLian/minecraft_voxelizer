#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // TextureAtlas.cpp owns stb_image_write
#include "tiny_gltf.h"

#include "io/GltfLoader.hpp"
#include <stdexcept>
#include <iostream>

Mesh GltfLoader::load(const std::string& path) {
    std::cout << "[GltfLoader] Loading: " << path << "\n";

    tinygltf::Model    gltfModel;
    tinygltf::TinyGLTF loader;
    std::string        err, warn;

    bool ok;
    std::string ext = path.substr(path.rfind('.') + 1);
    if (ext == "glb")
        ok = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);

    if (!warn.empty()) std::cerr << "[GltfLoader] Warning: " << warn << "\n";
    if (!ok)
        throw std::runtime_error("GltfLoader: " + err);

    Mesh mesh;

    // ── Materials ─────────────────────────────────────────────────────────────
    Mesh::Material defaultMat;
    defaultMat.name      = "default";
    defaultMat.baseColor = glm::vec3(0.8f);
    mesh.materials.push_back(defaultMat);

    for (const auto& gltfMat : gltfModel.materials) {
        Mesh::Material mat;
        mat.name      = gltfMat.name;
        mat.baseColor = extractBaseColor(gltfMat);
        mesh.materials.push_back(mat);
    }

    // ── Geometry ──────────────────────────────────────────────────────────────
    for (const auto& node : gltfModel.nodes) {
        if (node.mesh < 0) continue;
        const auto& gltfMesh = gltfModel.meshes[node.mesh];

        for (const auto& prim : gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // ── Helper: get accessor data as typed span ────────────────────
            auto getAccessor = [&](int accId) -> const tinygltf::Accessor* {
                if (accId < 0) return nullptr;
                return &gltfModel.accessors[accId];
            };
            auto getBuffer = [&](const tinygltf::Accessor* acc)
                -> const uint8_t* {
                if (!acc) return nullptr;
                const auto& bv  = gltfModel.bufferViews[acc->bufferView];
                const auto& buf = gltfModel.buffers[bv.buffer];
                return buf.data.data() + bv.byteOffset + acc->byteOffset;
            };

            // ── Positions ─────────────────────────────────────────────────
            auto posAcc  = getAccessor(prim.attributes.count("POSITION")
                           ? prim.attributes.at("POSITION") : -1);
            auto uvAcc   = getAccessor(prim.attributes.count("TEXCOORD_0")
                           ? prim.attributes.at("TEXCOORD_0") : -1);
            auto normAcc = getAccessor(prim.attributes.count("NORMAL")
                           ? prim.attributes.at("NORMAL") : -1);

            if (!posAcc) continue;

            const float* positions = reinterpret_cast<const float*>(getBuffer(posAcc));
            const float* uvs       = uvAcc   ? reinterpret_cast<const float*>(getBuffer(uvAcc))   : nullptr;
            const float* normals   = normAcc ? reinterpret_cast<const float*>(getBuffer(normAcc)) : nullptr;

            int baseVertex = static_cast<int>(mesh.vertices.size());

            for (size_t i = 0; i < posAcc->count; i++) {
                Mesh::Vertex v;
                v.position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                if (uvs)     v.uv     = glm::vec2(uvs[i*2],     uvs[i*2+1]);
                if (normals) v.normal = glm::vec3(normals[i*3], normals[i*3+1], normals[i*3+2]);
                mesh.vertices.push_back(v);
            }

            // ── Indices ───────────────────────────────────────────────────
            int matIndex = (prim.material >= 0) ? prim.material + 1 : 0;

            if (prim.indices >= 0) {
                const auto& idxAcc = gltfModel.accessors[prim.indices];
                const uint8_t* idxBuf = getBuffer(&idxAcc);

                auto getIndex = [&](size_t i) -> int {
                    switch (idxAcc.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            return reinterpret_cast<const uint16_t*>(idxBuf)[i];
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            return reinterpret_cast<const uint32_t*>(idxBuf)[i];
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            return idxBuf[i];
                        default: return 0;
                    }
                };

                for (size_t i = 0; i + 2 < idxAcc.count; i += 3) {
                    Mesh::Triangle tri;
                    tri.vertexIndices = {
                        baseVertex + getIndex(i),
                        baseVertex + getIndex(i + 1),
                        baseVertex + getIndex(i + 2)
                    };
                    tri.materialIndex = matIndex;
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }

    std::cout << "[GltfLoader] Loaded " << mesh.vertices.size()   << " vertices, "
              <<                           mesh.triangles.size()   << " triangles.\n";

    if (mesh.isEmpty())
        throw std::runtime_error("GltfLoader: file produced an empty mesh: " + path);

    return mesh;
}

glm::vec3 GltfLoader::extractBaseColor(const tinygltf::Material& mat) const {
    const auto& pbr = mat.pbrMetallicRoughness;
    const auto& cf  = pbr.baseColorFactor;
    if (cf.size() >= 3)
        return glm::vec3(
            static_cast<float>(cf[0]),
            static_cast<float>(cf[1]),
            static_cast<float>(cf[2])
        );
    return glm::vec3(0.8f);
}
