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

FetchContent_Declare(fpng
        GIT_REPOSITORY https://github.com/richgel999/fpng.git
        GIT_TAG        main
)

FetchContent_MakeAvailable(glm nlohmann_json tinyobjloader tinygltf stb fpng)

# Expose tinygltf's source dir for tiny_gltf.h includes
FetchContent_GetProperties(tinygltf SOURCE_DIR tinygltf_SOURCE_DIR)
# Expose stb source dir for stb_image_write.h (still needed for stb_image decode)
FetchContent_GetProperties(stb SOURCE_DIR stb_SOURCE_DIR)
# Expose fpng source dir
FetchContent_GetProperties(fpng SOURCE_DIR fpng_SOURCE_DIR)

# ── OpenMP ─────────────────────────────────────────────────────────────────────
find_package(OpenMP)

file(GLOB_RECURSE SOURCES
        "src/*.cpp"
)

# fpng is a single .cpp file that must be compiled into the project.
# FPNG_NO_SSE disables SSE4.1 intrinsics — avoids MinGW "target specific
# option mismatch" errors while still being significantly faster than stb.
add_executable(mc_voxelizer ${SOURCES} ${fpng_SOURCE_DIR}/src/fpng.cpp app.rc)

# Define your version
set(APP_VERSION "v1.4.0")

# Tell CMake to rename the output file
set_target_properties(mc_voxelizer PROPERTIES OUTPUT_NAME "mc_voxelizer-${APP_VERSION}")

target_compile_definitions(mc_voxelizer PRIVATE FPNG_NO_SSE=1)

target_include_directories(mc_voxelizer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        src/
        ${stb_SOURCE_DIR}
        ${tinygltf_SOURCE_DIR}
        ${fpng_SOURCE_DIR}/src
)

target_link_libraries(mc_voxelizer PRIVATE
        glm::glm
        nlohmann_json::nlohmann_json
        tinyobjloader
        tinygltf
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(mc_voxelizer PRIVATE OpenMP::OpenMP_CXX)
    # MinGW with static linking needs libgomp explicitly
    if(WIN32)
        target_link_libraries(mc_voxelizer PRIVATE -lgomp)
    endif()
    message(STATUS "OpenMP found — parallel baking enabled")
else()
    message(WARNING "OpenMP not found — baking will be single-threaded")
endif()

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