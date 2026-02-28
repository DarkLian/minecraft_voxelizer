#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// stb_image.h is implemented in GltfLoader.cpp (STB_IMAGE_IMPLEMENTATION
// defined there).  We only need the declarations here.
#include "stb_image.h"

#include "io/ObjLoader.hpp"
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
    // Index 0 = default grey fallback (no MTL assigned)
    {
        Mesh::Material def;
        def.name      = "default";
        def.baseColor = glm::vec3(0.8f);
        mesh.materials.push_back(def);
    }

    for (const auto &m : materials) {
        Mesh::Material mat;
        mat.name      = m.name;
        mat.baseColor = glm::vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
        mat.flipV     = true;  // OBJ UV convention: V=0 at bottom

        if (!m.diffuse_texname.empty()) {
            std::string texPath =
                (std::filesystem::path(path).parent_path() / m.diffuse_texname).string();
            mat.texturePath = texPath;

            // ── Load texture via stb_image ─────────────────────────────────
            int w = 0, h = 0, ch = 0;
            // Request 3 channels (RGB); stb_image converts RGBA/grey if needed
            uint8_t *data = stbi_load(texPath.c_str(), &w, &h, &ch, 3);
            if (data) {
                mat.imageW        = w;
                mat.imageH        = h;
                mat.imageChannels = 3;
                mat.imageData.assign(data, data + static_cast<size_t>(w) * h * 3);
                stbi_image_free(data);
                std::cout << "[ObjLoader]   Loaded texture: " << texPath
                          << " (" << w << "×" << h << ")\n";
            } else {
                std::cerr << "[ObjLoader]   WARNING: could not load texture: "
                          << texPath << " — " << stbi_failure_reason() << "\n";
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
            int matId = shape.mesh.material_ids[f];
            tri.materialIndex = (matId < 0) ? 0 : matId + 1;

            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                Mesh::Vertex vert;
                vert.position = glm::vec3(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                );
                if (idx.texcoord_index >= 0) {
                    vert.uv = glm::vec2(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    );
                }
                if (idx.normal_index >= 0) {
                    vert.normal = glm::vec3(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    );
                }

                tri.vertexIndices[v] = static_cast<int>(mesh.vertices.size());
                mesh.vertices.push_back(vert);
            }

            mesh.triangles.push_back(tri);
            indexOffset += fv;
        }
    }

    // ── Material summary ──────────────────────────────────────────────────────
    std::cout << "[ObjLoader] Loaded " << mesh.vertices.size() << " vertices, "
              << mesh.triangles.size() << " triangles, "
              << mesh.materials.size() - 1 << " materials.\n";
    for (size_t mi = 1; mi < mesh.materials.size(); mi++) {
        const auto &mat = mesh.materials[mi];
        if (mat.hasTexture()) {
            std::cout << "[ObjLoader]   Mat[" << mi << "] '" << mat.name
                      << "': texture " << mat.imageW << "×" << mat.imageH << " px"
                      << " | baseColor(" << mat.baseColor.r << ","
                      << mat.baseColor.g << "," << mat.baseColor.b << ")\n";
        } else {
            glm::vec3 bc = mat.baseColor;
            std::cout << "[ObjLoader]   Mat[" << mi << "] '" << mat.name
                      << "': flat colour (" << bc.r << "," << bc.g << "," << bc.b << ")";
            if (!mat.texturePath.empty())
                std::cout << "  ← WARNING: texture failed to load!";
            else if (bc.r < 0.01f && bc.g < 0.01f && bc.b < 0.01f)
                std::cout << "  ← NOTE: baseColor is black; voxels will appear black.";
            std::cout << "\n";
        }
    }

    if (mesh.isEmpty())
        throw std::runtime_error("ObjLoader: file produced an empty mesh: " + path);

    return mesh;
}