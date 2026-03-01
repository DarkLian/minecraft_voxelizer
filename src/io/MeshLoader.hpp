#pragma once

#include "core/Mesh.hpp"
#include <string>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// MeshLoader — abstract base class for all 3D file format loaders.
// New formats (FBX, DAE, etc.) can be added by subclassing this.
//
// OOP patterns: Abstract class / polymorphism, Factory method.
// ─────────────────────────────────────────────────────────────────────────────
class MeshLoader {
public:
    virtual ~MeshLoader() = default;

    // Load a mesh from file. Throws std::runtime_error on failure.
    virtual Mesh load(const std::string &path) = 0;

    // ── Factory method ────────────────────────────────────────────────────────
    // Inspects the file extension and returns the appropriate concrete loader.
    // Usage: autoloader = MeshLoader::create("model.obj");
    static std::unique_ptr<MeshLoader> create(const std::string &path);

protected:
    // Utility shared by loaders: normalize a path to lowercase extension
    static std::string getExtension(const std::string &path);
};
