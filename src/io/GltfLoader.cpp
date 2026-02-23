#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // TextureAtlas.cpp owns stb_image_write
#include "tiny_gltf.h"

#include "io/GltfLoader.hpp"
#include <stdexcept>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Texture loading notes
// ─────────────────────────────────────────────────────────────────────────────
// tinygltf (with STB_IMAGE_IMPLEMENTATION defined above) automatically decodes
// every image referenced by the model — whether embedded as base64 in a .gltf
// file, or packed into the binary buffer of a .glb file — into raw pixel data
// stored in tinygltf::Image::image (vector<uint8_t>).
//
// We therefore never need to call stbi_load ourselves for glTF/glb; we just
// copy the already-decoded pixels into our Mesh::Material::TextureImage.
//
// Component count from tinygltf:
//   1 = greyscale    → replicate R into R,G,B and set A=255
//   2 = grey+alpha   → replicate R into R,G,B, keep A
//   3 = RGB          → add A=255
//   4 = RGBA         → copy directly
//
// All cases are normalised to RGBA (4 channels) to match what
// Mesh::sampleColor expects.
// ─────────────────────────────────────────────────────────────────────────────

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
    if (!ok)
        throw std::runtime_error("GltfLoader: " + err);

    Mesh mesh;

    // ── Materials ─────────────────────────────────────────────────────────────
    // Index 0 is the white fallback; glTF materials start at index 1.
    {
        Mesh::Material def;
        def.name      = "default";
        def.baseColor = glm::vec3(0.8f);
        mesh.materials.push_back(def);
    }

    for (const auto &gltfMat : gltfModel.materials) {
        Mesh::Material mat;
        mat.name      = gltfMat.name;
        mat.baseColor = extractBaseColor(gltfMat); // fallback if no texture

        // ── Try to load the base-color texture ────────────────────────────────
        const auto &pbr = gltfMat.pbrMetallicRoughness;
        int texIdx = pbr.baseColorTexture.index; // -1 if not set

        if (texIdx >= 0 && texIdx < static_cast<int>(gltfModel.textures.size())) {
            int imgIdx = gltfModel.textures[texIdx].source;

            if (imgIdx >= 0 && imgIdx < static_cast<int>(gltfModel.images.size())) {
                const tinygltf::Image &img = gltfModel.images[imgIdx];

                // img.image is non-empty only when tinygltf successfully decoded it
                if (!img.image.empty() && img.width > 0 && img.height > 0) {
                    mat.texture.width  = img.width;
                    mat.texture.height = img.height;

                    // Normalise to 4-channel RGBA
                    int ch = img.component; // 1, 2, 3 or 4
                    int npix = img.width * img.height;
                    mat.texture.pixels.resize(static_cast<size_t>(npix * 4));

                    for (int i = 0; i < npix; i++) {
                        uint8_t r, g, b, a;
                        const uint8_t *src = img.image.data() + i * ch;

                        switch (ch) {
                            case 1:                       // greyscale
                                r = g = b = src[0];
                                a = 255;
                                break;
                            case 2:                       // greyscale + alpha
                                r = g = b = src[0];
                                a = src[1];
                                break;
                            case 3:                       // RGB
                                r = src[0]; g = src[1]; b = src[2];
                                a = 255;
                                break;
                            default:                      // RGBA (ch == 4)
                                r = src[0]; g = src[1];
                                b = src[2]; a = src[3];
                                break;
                        }

                        uint8_t *dst = mat.texture.pixels.data() + i * 4;
                        dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
                    }

                    std::cout << "[GltfLoader]   Loaded texture for material '"
                              << mat.name << "': "
                              << img.width << "×" << img.height
                              << " (" << ch << "ch → RGBA)\n";
                } else {
                    std::cerr << "[GltfLoader]   Warning: image " << imgIdx
                              << " for material '" << mat.name
                              << "' has no decoded pixel data. "
                              << "Falling back to baseColor.\n";
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

            auto getAccessor = [&](int accId) -> const tinygltf::Accessor * {
                return (accId >= 0) ? &gltfModel.accessors[accId] : nullptr;
            };
            auto getBuffer = [&](const tinygltf::Accessor *acc) -> const uint8_t * {
                if (!acc) return nullptr;
                const auto &bv  = gltfModel.bufferViews[acc->bufferView];
                const auto &buf = gltfModel.buffers[bv.buffer];
                return buf.data.data() + bv.byteOffset + acc->byteOffset;
            };

            auto posAcc  = getAccessor(prim.attributes.count("POSITION")
                               ? prim.attributes.at("POSITION") : -1);
            auto uvAcc   = getAccessor(prim.attributes.count("TEXCOORD_0")
                               ? prim.attributes.at("TEXCOORD_0") : -1);
            auto normAcc = getAccessor(prim.attributes.count("NORMAL")
                               ? prim.attributes.at("NORMAL") : -1);

            if (!posAcc) continue;

            const auto *positions = reinterpret_cast<const float *>(getBuffer(posAcc));
            const auto *uvs       = uvAcc   ? reinterpret_cast<const float *>(getBuffer(uvAcc))   : nullptr;
            const auto *normals   = normAcc ? reinterpret_cast<const float *>(getBuffer(normAcc)) : nullptr;

            int baseVertex = static_cast<int>(mesh.vertices.size());

            for (size_t i = 0; i < posAcc->count; i++) {
                Mesh::Vertex v;
                v.position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                if (uvs)     v.uv     = glm::vec2(uvs[i*2],       uvs[i*2+1]);
                if (normals) v.normal = glm::vec3(normals[i*3], normals[i*3+1], normals[i*3+2]);
                mesh.vertices.push_back(v);
            }

            // +1 offset because mesh.materials[0] is our white fallback
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
                    tri.vertexIndices  = {
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

    std::cout << "[GltfLoader] Loaded " << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles, "
              << (mesh.materials.size() - 1) << " material(s).\n";

    if (mesh.isEmpty())
        throw std::runtime_error("GltfLoader: empty mesh: " + path);

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