cmake_minimum_required(VERSION 3.20)
project(minecraft_voxelizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via FetchContent ──────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_Declare(tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
)

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(glm nlohmann_json tinyobjloader tinygltf stb)

# Expose tinygltf's source dir for tiny_gltf.h includes
FetchContent_GetProperties(tinygltf SOURCE_DIR tinygltf_SOURCE_DIR)
# Expose stb source dir for stb_image_write.h
FetchContent_GetProperties(stb SOURCE_DIR stb_SOURCE_DIR)


file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

add_executable(mc_voxelizer ${SOURCES})

target_include_directories(mc_voxelizer PRIVATE
    src/
    ${stb_SOURCE_DIR}
    ${tinygltf_SOURCE_DIR}
)

target_link_libraries(mc_voxelizer PRIVATE
    glm::glm
    nlohmann_json::nlohmann_json
    tinyobjloader
    tinygltf
)

# Optimizations for release builds
target_compile_options(mc_voxelizer PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -Wall -Wextra>
)

# Force static linking on Windows so it doesn't require MinGW DLLs to run
if(WIN32)
    target_link_options(mc_voxelizer PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ── Install / output dir ───────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
