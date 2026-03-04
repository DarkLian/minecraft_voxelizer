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

# ── Version ────────────────────────────────────────────────────────────────────
set(APP_VERSION "v1.5.0")

# ── Install / output dir ───────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Shared sources — exclude all variant files ────────────────────────────────
file(GLOB_RECURSE SHARED_SOURCES "src/*.cpp")
list(FILTER SHARED_SOURCES EXCLUDE REGEX ".*/pipeline/(atlas|element)/.*\\.cpp$")
list(FILTER SHARED_SOURCES EXCLUDE REGEX ".*McModel_(atlas|element)\\.cpp$")

# ── Shared config macro ────────────────────────────────────────────────────────
macro(configure_voxelizer_target TARGET_NAME PIPELINE_VARIANT_DIR)
    target_compile_definitions(${TARGET_NAME} PRIVATE FPNG_NO_SSE=1)

    # PIPELINE_VARIANT_DIR must come BEFORE src/ so its GreedyMesher.hpp
    # shadows the include path and gets picked up instead of any stale one.
    target_include_directories(${TARGET_NAME} PRIVATE
            ${PIPELINE_VARIANT_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}
            src/
            ${stb_SOURCE_DIR}
            ${tinygltf_SOURCE_DIR}
            ${fpng_SOURCE_DIR}/src
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
            glm::glm
            nlohmann_json::nlohmann_json
            tinyobjloader
            tinygltf
    )

    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TARGET_NAME} PRIVATE OpenMP::OpenMP_CXX)
        if(WIN32)
            target_link_libraries(${TARGET_NAME} PRIVATE -lgomp)
        endif()
    endif()

    target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-g -Wall -Wextra>
    )

    if(WIN32)
        target_link_options(${TARGET_NAME} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endmacro()

# ── Element-count optimization ─────────────────────────────────────────────────
add_executable(mc_voxelizer_element
        ${SHARED_SOURCES}
        ${fpng_SOURCE_DIR}/src/fpng.cpp
        app.rc
        src/pipeline/element/GreedyMesher.cpp
        src/minecraft/McModel_element.cpp
)
set_target_properties(mc_voxelizer_element PROPERTIES
        OUTPUT_NAME "mc_voxelizer_element-${APP_VERSION}")
configure_voxelizer_target(mc_voxelizer_element
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/element)

# ── Atlas optimization ─────────────────────────────────────────────────────────
add_executable(mc_voxelizer_atlas
        ${SHARED_SOURCES}
        ${fpng_SOURCE_DIR}/src/fpng.cpp
        app.rc
        src/pipeline/atlas/GreedyMesher.cpp
        src/minecraft/McModel_atlas.cpp
)
set_target_properties(mc_voxelizer_atlas PROPERTIES
        OUTPUT_NAME "mc_voxelizer_atlas-${APP_VERSION}")
configure_voxelizer_target(mc_voxelizer_atlas
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/atlas)

# ── OpenMP status messages ─────────────────────────────────────────────────────
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found — parallel baking enabled")
else()
    message(WARNING "OpenMP not found — baking will be single-threaded")
endif()