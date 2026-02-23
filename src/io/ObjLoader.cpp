#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// stb_image implementation lives in GltfLoader.cpp (STB_IMAGE_IMPLEMENTATION).
// Include here without re-defining it to get stbi_load for texture loading.
#include "stb_image.h"

#include "io/ObjLoader.hpp"

#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <filesystem>

Mesh ObjLoader::load(const std::string &path) {
    std::cout << "[ObjLoader] Loading: " << path << "\n";

    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = std::filesystem::path(path).parent_path().string();
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config))
        throw std::runtime_error("ObjLoader: " + reader.Error());

    if (!reader.Warning().empty())
        std::cerr << "[ObjLoader] Warning: " << reader.Warning() << "\n";

    const auto &attrib    = reader.GetAttrib();
    const auto &shapes    = reader.GetShapes();
    const auto &materials = reader.GetMaterials();

    Mesh mesh;

    // ── Materials ─────────────────────────────────────────────────────────────
    // Index 0 is always the white fallback; MTL materials start at index 1.
    {
        Mesh::Material def;
        def.name      = "default";
        def.baseColor = glm::vec3(0.8f);
        mesh.materials.push_back(def);
    }

    std::string texDir = std::filesystem::path(path).parent_path().string();

    for (const auto &m : materials) {
        Mesh::Material mat;
        mat.name      = m.name;
        mat.baseColor = glm::vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);

        if (!m.diffuse_texname.empty()) {
            // Resolve the texture path relative to the OBJ directory.
            // tinyobjloader stores the path as written in the MTL, which may
            // use backslashes on Windows — normalize to forward slashes.
            std::string rawName = m.diffuse_texname;
            std::replace(rawName.begin(), rawName.end(), '\\', '/');

            mat.texturePath =
                (std::filesystem::path(texDir) / rawName).string();

            // ── Load the image with stb_image ─────────────────────────────────
            //
            // stbi_load decodes the image into RGBA (4 channels), which is what
            // Mesh::sampleColor expects. If the file can't be opened (wrong path,
            // missing file, etc.) we warn and fall back to baseColor.
            int w = 0, h = 0, ch = 0;
            uint8_t *data = stbi_load(mat.texturePath.c_str(),
                                      &w, &h, &ch, 4 /*force RGBA*/);
            if (data) {
                mat.texture.width  = w;
                mat.texture.height = h;
                mat.texture.pixels.assign(data, data + w * h * 4);
                stbi_image_free(data);

                std::cout << "[ObjLoader]   Loaded texture: "
                          << mat.texturePath
                          << " (" << w << "×" << h << ")\n";
            } else {
                std::cerr << "[ObjLoader]   Warning: could not load texture '"
                          << mat.texturePath << "' — "
                          << stbi_failure_reason()
                          << ". Falling back to diffuse color.\n";
            }
        }

        mesh.materials.push_back(mat);
    }

    // ── Geometry ──────────────────────────────────────────────────────────────
    for (const auto &shape : shapes) {
        size_t indexOffset = 0;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) { indexOffset += fv; continue; }

            Mesh::Triangle tri;
            int matId       = shape.mesh.material_ids[f];
            tri.materialIndex = (matId < 0) ? 0 : matId + 1;

            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                Mesh::Vertex vert;
                vert.position = glm::vec3(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]);

                if (idx.texcoord_index >= 0) {
                    vert.uv = glm::vec2(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]);
                }

                if (idx.normal_index >= 0) {
                    vert.normal = glm::vec3(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]);
                }

                tri.vertexIndices[v] = static_cast<int>(mesh.vertices.size());
                mesh.vertices.push_back(vert);
            }

            mesh.triangles.push_back(tri);
            indexOffset += fv;
        }
    }

    std::cout << "[ObjLoader] Loaded " << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles, "
              << (mesh.materials.size() - 1) << " material(s).\n";

    if (mesh.isEmpty())
        throw std::runtime_error("ObjLoader: empty mesh: " + path);

    return mesh;
}