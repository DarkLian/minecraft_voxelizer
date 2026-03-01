#pragma once

#include "io/MeshLoader.hpp"

// Forward declare to avoid pulling all of tinygltf into every TU that
// includes this header. The full include is in GltfLoader.cpp only.
namespace tinygltf {
    struct Material;
}

// ─────────────────────────────────────────────────────────────────────────────
// GltfLoader — loads .gltf and .glb files using tinygltf.
// ─────────────────────────────────────────────────────────────────────────────
class GltfLoader : public MeshLoader {
public:
    Mesh load(const std::string &path) override;

private:
    [[nodiscard]] static glm::vec3 extractBaseColor(const tinygltf::Material &mat);
};
