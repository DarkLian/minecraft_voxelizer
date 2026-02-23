#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "io/ObjLoader.hpp"
#include <stdexcept>
#include <iostream>
#include <filesystem>

Mesh ObjLoader::load(const std::string& path) {
    std::cout << "[ObjLoader] Loading: " << path << "\n";

    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = std::filesystem::path(path).parent_path().string();
    config.triangulate = true; // ensure all faces are triangles

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        throw std::runtime_error("ObjLoader: " + reader.Error());
    }

    if (!reader.Warning().empty())
        std::cerr << "[ObjLoader] Warning: " << reader.Warning() << "\n";

    const auto& attrib   = reader.GetAttrib();
    const auto& shapes   = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    Mesh mesh;

    // ── Materials ─────────────────────────────────────────────────────────────
    // Add a default white material at index 0 as fallback
    {
        Mesh::Material defaultMat;
        defaultMat.name      = "default";
        defaultMat.baseColor = glm::vec3(0.8f); // light grey fallback
        mesh.materials.push_back(defaultMat);
    }

    for (const auto& m : materials) {
        Mesh::Material mat;
        mat.name = m.name;
        mat.baseColor = glm::vec3(
            m.diffuse[0],
            m.diffuse[1],
            m.diffuse[2]
        );
        if (!m.diffuse_texname.empty()) {
            // Resolve relative texture path
            mat.texturePath =
                (std::filesystem::path(path).parent_path() / m.diffuse_texname).string();
        }
        mesh.materials.push_back(mat);
    }

    // ── Geometry ──────────────────────────────────────────────────────────────
    // tinyobjloader indexes vertices per-face, so we build a flat vertex list.
    for (const auto& shape : shapes) {
        size_t indexOffset = 0;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            // All faces are triangulated by tinyobjloader (config.triangulate=true)
            int fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) continue; // safety guard

            Mesh::Triangle tri;

            // Material for this face (+1 offset because index 0 = our default mat)
            int matId = shape.mesh.material_ids[f];
            tri.materialIndex = (matId < 0) ? 0 : matId + 1;

            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                Mesh::Vertex vert;

                // Position (required)
                vert.position = glm::vec3(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                );

                // UV (optional)
                if (idx.texcoord_index >= 0) {
                    vert.uv = glm::vec2(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    );
                }

                // Normal (optional)
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

    std::cout << "[ObjLoader] Loaded " << mesh.vertices.size()   << " vertices, "
              <<                          mesh.triangles.size()   << " triangles, "
              <<                          mesh.materials.size()-1 << " materials.\n";

    if (mesh.isEmpty())
        throw std::runtime_error("ObjLoader: file produced an empty mesh: " + path);

    return mesh;
}
