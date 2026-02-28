#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // TextureAtlas.cpp owns stb_image_write
#include "tiny_gltf.h"

#include "io/GltfLoader.hpp"
#include <stdexcept>
#include <iostream>

Mesh GltfLoader::load(const std::string &path) {
    std::cout << "[GltfLoader] Loading: " << path << "\n";

    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok;
    std::string ext = path.substr(path.rfind('.') + 1);
    if (ext == "glb")
        ok = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);

    if (!warn.empty()) std::cerr << "[GltfLoader] Warning: " << warn << "\n";
    if (!ok)           throw std::runtime_error("GltfLoader: " + err);

    Mesh mesh;

    // ── Materials ─────────────────────────────────────────────────────────────
    // Index 0 = default grey fallback
    {
        Mesh::Material def;
        def.name      = "default";
        def.baseColor = glm::vec3(0.8f);
        mesh.materials.push_back(def);
    }

    for (const auto &gltfMat : gltfModel.materials) {
        Mesh::Material mat;
        mat.name      = gltfMat.name;
        mat.baseColor = extractBaseColor(gltfMat);
        mat.flipV     = false; // glTF 2.0: V=0 at top — no flip needed

        // ── Extract base-colour texture ────────────────────────────────────
        int texIdx = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIdx >= 0 && texIdx < static_cast<int>(gltfModel.textures.size())) {
            int imgIdx = gltfModel.textures[texIdx].source;
            if (imgIdx >= 0 && imgIdx < static_cast<int>(gltfModel.images.size())) {
                const auto &img = gltfModel.images[imgIdx];

                // tinygltf uses stb_image to decode embedded/external images
                // into img.image (vector<uint8_t>), img.width, img.height,
                // img.component.
                if (!img.image.empty() && img.width > 0 && img.height > 0) {
                    int ch = img.component;  // 1, 2, 3, or 4

                    if (ch >= 3) {
                        // Copy and drop alpha if present (keep only RGB)
                        mat.imageW        = img.width;
                        mat.imageH        = img.height;
                        mat.imageChannels = 3;
                        mat.imageData.reserve(static_cast<size_t>(img.width) * img.height * 3);

                        for (size_t px = 0; px < static_cast<size_t>(img.width) * img.height; px++) {
                            mat.imageData.push_back(img.image[px * ch + 0]); // R
                            mat.imageData.push_back(img.image[px * ch + 1]); // G
                            mat.imageData.push_back(img.image[px * ch + 2]); // B
                        }

                        std::cout << "[GltfLoader]   Loaded texture for '"
                                  << mat.name << "' ("
                                  << img.width << "×" << img.height
                                  << ", " << ch << " ch)\n";
                    } else {
                        // Greyscale — expand to RGB
                        mat.imageW        = img.width;
                        mat.imageH        = img.height;
                        mat.imageChannels = 3;
                        mat.imageData.reserve(static_cast<size_t>(img.width) * img.height * 3);

                        for (size_t px = 0; px < static_cast<size_t>(img.width) * img.height; px++) {
                            uint8_t grey = img.image[px * ch];
                            mat.imageData.push_back(grey);
                            mat.imageData.push_back(grey);
                            mat.imageData.push_back(grey);
                        }
                    }
                } else {
                    std::cerr << "[GltfLoader]   WARNING: texture for '"
                              << mat.name << "' could not be decoded.\n";
                }
            }
        }

        mesh.materials.push_back(mat);
    }

    // ── Geometry ──────────────────────────────────────────────────────────────
    for (const auto &node : gltfModel.nodes) {
        if (node.mesh < 0) continue;
        const auto &gltfMesh = gltfModel.meshes[node.mesh];

        for (const auto &prim : gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            auto getAccessor = [&](int id) -> const tinygltf::Accessor * {
                return (id < 0) ? nullptr : &gltfModel.accessors[id];
            };
            auto getBuffer = [&](const tinygltf::Accessor *acc) -> const uint8_t * {
                if (!acc) return nullptr;
                const auto &bv  = gltfModel.bufferViews[acc->bufferView];
                const auto &buf = gltfModel.buffers[bv.buffer];
                return buf.data.data() + bv.byteOffset + acc->byteOffset;
            };

            auto posAcc  = getAccessor(prim.attributes.count("POSITION")    ? prim.attributes.at("POSITION")    : -1);
            auto uvAcc   = getAccessor(prim.attributes.count("TEXCOORD_0")  ? prim.attributes.at("TEXCOORD_0")  : -1);
            auto normAcc = getAccessor(prim.attributes.count("NORMAL")      ? prim.attributes.at("NORMAL")      : -1);

            if (!posAcc) continue;

            const float *positions = reinterpret_cast<const float *>(getBuffer(posAcc));
            const float *uvs       = uvAcc   ? reinterpret_cast<const float *>(getBuffer(uvAcc))   : nullptr;
            const float *normals   = normAcc ? reinterpret_cast<const float *>(getBuffer(normAcc)) : nullptr;

            int baseVertex = static_cast<int>(mesh.vertices.size());

            for (size_t i = 0; i < posAcc->count; i++) {
                Mesh::Vertex v;
                v.position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                if (uvs)     v.uv     = glm::vec2(uvs[i*2],     uvs[i*2+1]);
                if (normals) v.normal = glm::vec3(normals[i*3], normals[i*3+1], normals[i*3+2]);
                mesh.vertices.push_back(v);
            }

            int matIndex = (prim.material >= 0) ? prim.material + 1 : 0;

            if (prim.indices >= 0) {
                const auto &idxAcc = gltfModel.accessors[prim.indices];
                const uint8_t *idxBuf = getBuffer(&idxAcc);

                auto getIndex = [&](size_t i) -> int {
                    switch (idxAcc.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            return reinterpret_cast<const uint16_t *>(idxBuf)[i];
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            return reinterpret_cast<const uint32_t *>(idxBuf)[i];
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            return idxBuf[i];
                        default: return 0;
                    }
                };

                for (size_t i = 0; i + 2 < idxAcc.count; i += 3) {
                    Mesh::Triangle tri;
                    tri.vertexIndices  = { baseVertex + getIndex(i),
                                           baseVertex + getIndex(i+1),
                                           baseVertex + getIndex(i+2) };
                    tri.materialIndex  = matIndex;
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }

    std::cout << "[GltfLoader] Loaded " << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles.\n";

    if (mesh.isEmpty())
        throw std::runtime_error("GltfLoader: file produced an empty mesh: " + path);

    return mesh;
}

glm::vec3 GltfLoader::extractBaseColor(const tinygltf::Material &mat) const {
    const auto &cf = mat.pbrMetallicRoughness.baseColorFactor;
    if (cf.size() >= 3)
        return glm::vec3(static_cast<float>(cf[0]),
                         static_cast<float>(cf[1]),
                         static_cast<float>(cf[2]));
    return glm::vec3(0.8f);
}