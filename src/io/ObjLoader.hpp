#pragma once

#include "io/MeshLoader.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ObjLoader — loads Wavefront .obj files using tinyobjloader.
// Handles materials (.mtl), per-face UVs, and multiple material groups.
// ─────────────────────────────────────────────────────────────────────────────
class ObjLoader : public MeshLoader {
public:
    Mesh load(const std::string &path) override;
};