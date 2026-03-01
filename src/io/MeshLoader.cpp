#include "io/MeshLoader.hpp"
#include "io/ObjLoader.hpp"
#include "io/GltfLoader.hpp"
#include <algorithm>
#include <stdexcept>

std::string MeshLoader::getExtension(const std::string &path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos + 1);
    std::ranges::transform(ext, ext.begin(), ::tolower);
    return ext;
}

std::unique_ptr<MeshLoader> MeshLoader::create(const std::string &path) {
    std::string ext = getExtension(path);

    if (ext == "obj")
        return std::make_unique<ObjLoader>();

    if (ext == "gltf" || ext == "glb")
        return std::make_unique<GltfLoader>();

    throw std::runtime_error(
        "MeshLoader: unsupported file extension '." + ext + "'. "
        "Supported formats: .obj, .gltf, .glb"
    );
}