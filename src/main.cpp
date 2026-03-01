#include "io/MeshLoader.hpp"
#include "pipeline/Normalizer.hpp"
#include "pipeline/Voxelizer.hpp"
#include "pipeline/GreedyMesher.hpp"
#include "core/TextureAtlas.hpp"
#include "minecraft/McModel.hpp"

#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Timer helper ──────────────────────────────────────────────────────────────
using Clock = std::chrono::steady_clock;
using TP = std::chrono::time_point<Clock>;

static TP now() { return Clock::now(); }

static std::string elapsed(TP start, TP end) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (ms < 1000)
        return std::to_string(ms) + " ms";
    // Show seconds with one decimal for longer stages
    double s = ms / 1000.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f s", s);
    return buf;
}

struct CliArgs {
    std::string inputPath;
    std::string outputDir = "./output";
    std::string modelName;
    std::string modId = "mymod";
    int quality = 3;
    int density = 0; // 0 = auto (computed after mesh load)
    bool solidFill = false;
    bool help = false;
};

// ── Compute recommended density ───────────────────────────────────────────────
// Optimal density = source_texture_size / grid_resolution, clamped to [1, 32].
// If the mesh has no textures we return 1 (flat color, density doesn't help).
static int recommendedDensity(const Mesh &mesh, int gridRes) {
    int maxTexSize = 0;
    for (const auto &mat: mesh.materials)
        if (mat.hasTexture())
            maxTexSize = std::max(maxTexSize, std::max(mat.imageW, mat.imageH));

    if (maxTexSize == 0) return 1;

    // Round to nearest power of two for cleaner atlas subdivision
    int raw = std::max(1, maxTexSize / gridRes);
    int p = 1;
    while (p * 2 <= raw) p *= 2;
    return std::min(p, 32);
}

static void printUsage() {
    std::cout <<
            "Usage: mc_voxelizer <input.obj|.gltf|.glb> [options]\n"
            "\n"
            "Options:\n"
            "  --quality  1-7    Voxel resolution (default: 3)\n"
            "                      1 =  16^3  fastest/blockiest\n"
            "                      3 =  32^3  recommended default\n"
            "                      5 =  64^3  good detail\n"
            "                      6 =  96^3  face expressions visible\n"
            "                      7 = 128^3  maximum detail (slow)\n"
            "  --density  N      Texture pixels per voxel face (default: auto)\n"
            "                    Auto = source_texture_size / grid_resolution\n"
            "                    e.g. 1024px texture + quality 7 (128^3) => density 8\n"
            "                    Going higher than auto blurs with no extra detail.\n"
            "  --output   <dir>  Output directory (default: ./output)\n"
            "  --name     <str>  Model/file name (default: input filename stem)\n"
            "  --modid    <str>  Mod namespace for texture path (default: mymod)\n"
            "  --solid           Fill solid interior voxels (default: off)\n"
            "  --help            Show this help\n"
            "\n"
            "Density sweet spot by texture size:\n"
            "  Texture    Q3(32)  Q4(48)  Q5(64)  Q6(96)  Q7(128)\n"
            "   256x256     8       5       4       2        2\n"
            "   512x512    16      10       8       5        4\n"
            "  1024x1024   32      21      16       8        8\n"
            "  2048x2048   32      32      32      16       16\n"
            "\n"
            "Character face tip: use quality 6 or 7 for anime/detailed faces.\n"
            "  mc_voxelizer character.glb --quality 6 --density 8\n"
            "  mc_voxelizer character.glb --quality 7 --density 8\n";
}

static CliArgs parseArgs(int argc, char **argv) {
    CliArgs args;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            args.help = true;
            return args;
        }

        if (arg == "--quality" && i + 1 < argc) {
            args.quality = std::stoi(argv[++i]);
            if (args.quality < 1 || args.quality > 7)
                throw std::invalid_argument("--quality must be 1-7.");
        } else if (arg == "--density" && i + 1 < argc) {
            args.density = std::stoi(argv[++i]);
            if (args.density < 1 || args.density > 64)
                throw std::invalid_argument("--density must be 1-64.");
        } else if (arg == "--output" && i + 1 < argc) {
            args.outputDir = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            args.modelName = argv[++i];
        } else if (arg == "--modid" && i + 1 < argc) {
            args.modId = argv[++i];
        } else if (arg == "--solid") {
            args.solidFill = true;
        } else if (arg[0] != '-') {
            args.inputPath = arg;
        } else {
            throw std::invalid_argument("Unknown option: " + arg);
        }
    }
    if (args.inputPath.empty())
        throw std::invalid_argument("No input file specified.");
    if (args.modelName.empty())
        args.modelName = std::filesystem::path(args.inputPath).stem().string();
    return args;
}

static CliArgs interactivePrompt() {
    CliArgs args;
    std::cout << "--- Interactive Mode ---\n";
    std::cout << "Drag and drop your 3D model file here (or type the path):\n> ";
    std::string path;
    std::getline(std::cin, path);
    if (!path.empty() && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    args.inputPath = path;
    if (args.inputPath.empty())
        throw std::invalid_argument("No input file specified.");

    std::cout << "Enter voxel resolution quality (1-7) [Enter = 3]:\n"
            << "  1 =  16^3  fastest/blockiest\n"
            << "  3 =  32^3  recommended default\n"
            << "  5 =  64^3  good detail\n"
            << "  6 =  96^3  face expressions visible\n"
            << "  7 = 128^3  maximum detail (slow)\n> ";
    std::string q;
    std::getline(std::cin, q);
    if (!q.empty()) {
        args.quality = std::stoi(q);
        if (args.quality < 1 || args.quality > 7)
            throw std::invalid_argument("Quality must be 1-7.");
    }

    std::cout << "Enter texture density in pixels per voxel (1-64) [Enter = auto]:\n"
            << "  Auto = source_texture_size / grid_resolution (recommended)\n"
            << "  Sweet spot examples:\n"
            << "    512px texture  + quality 5 => 8\n"
            << "    1024px texture + quality 5 => 16\n"
            << "    2048px texture + quality 7 => 16\n"
            << "  Going above auto adds no detail, only increases PNG size.\n> ";
    std::string d;
    std::getline(std::cin, d);
    if (!d.empty()) {
        args.density = std::stoi(d);
        if (args.density < 1 || args.density > 64)
            throw std::invalid_argument("Density must be 1-64.");
    }
    // 0 = auto, resolved after mesh load

    std::cout << "Enable solid interior fill? (y/n) [Enter = y]:\n> ";
    std::string s;
    std::getline(std::cin, s);
    if (s == "n" || s == "N") args.solidFill = false;
    else args.solidFill = true;

    std::cout << "Enter mod ID [Enter = 'darkaddons']:\n> ";
    std::string m;
    std::getline(std::cin, m);
    args.modId = m.empty() ? "darkaddons" : m;

    std::cout << "Enter model name [Enter = use filename]:\n> ";
    std::string n;
    std::getline(std::cin, n);
    args.modelName = n.empty()
                         ? std::filesystem::path(args.inputPath).stem().string()
                         : n;

    std::cout << "\nStarting generation...\n";
    return args;
}

static void pauseConsole() {
    std::cout << "\nPress Enter to exit...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "╔══════════════════════════════════════╗\n"
            << "║   Minecraft Voxelizer  v1.3.0        ║\n"
            << "╚══════════════════════════════════════╝\n\n";

    CliArgs args;
    try {
        args = (argc < 2) ? interactivePrompt() : parseArgs(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        printUsage();
        pauseConsole();
        return 1;
    }
    if (args.help) {
        printUsage();
        return 0;
    }

    std::filesystem::create_directories(args.outputDir);
    std::string jsonOut = args.outputDir + "/" + args.modelName + ".json";
    std::string pngOut = args.outputDir + "/" + args.modelName + ".png";
    std::string texPath = args.modId + ":item/" + args.modelName;

    try {
        TP t0 = now();

        // ── Step 1: Load mesh ──────────────────────────────────────────────
        auto loader = MeshLoader::create(args.inputPath);
        Mesh mesh = loader->load(args.inputPath);
        TP t1 = now();

        // ── Step 2: Resolve density (auto if not set) ──────────────────────
        int gridRes = Voxelizer::qualityToResolution(args.quality);
        if (args.density == 0) {
            args.density = recommendedDensity(mesh, gridRes);
            std::cout << "[Density] Auto-selected density = " << args.density
                    << " (source_texture / grid = "
                    << gridRes << "^3)\n";
        } else {
            int recommended = recommendedDensity(mesh, gridRes);
            if (args.density > recommended && recommended > 1) {
                std::cout << "[Density] WARNING: density=" << args.density
                        << " exceeds recommended=" << recommended
                        << " for this texture+quality combination.\n"
                        << "          Pixels beyond the sweet spot are bilinearly"
                        << " upscaled — no extra detail is gained.\n";
            }
        }

        // ── Print settings ─────────────────────────────────────────────────
        std::cout << "\nInput:    " << args.inputPath << "\n"
                << "Output:   " << args.outputDir << "\n"
                << "Name:     " << args.modelName << "\n"
                << "Quality:  " << args.quality << " (" << gridRes << "^3)\n"
                << "Density:  " << args.density << " px/voxel"
                << "  (atlas ~" << (gridRes * args.density)
                << "x" << (gridRes * args.density) << " px before packing)\n"
                << "Texture:  " << texPath << "\n\n";

        // ── Step 3: Normalize to MC space ──────────────────────────────────
        TP t2 = now();
        Normalizer::Config normCfg;
        normCfg.snapFloor = true;
        Normalizer normalizer(normCfg);
        Mesh normalized = normalizer.normalize(mesh);
        TP t3 = now();

        // ── Step 4: Voxelize ───────────────────────────────────────────────
        Voxelizer::Config voxCfg;
        voxCfg.quality = args.quality;
        voxCfg.solidFill = args.solidFill;
        voxCfg.verbose = true;
        Voxelizer voxelizer(voxCfg);
        VoxelGrid grid = voxelizer.voxelize(normalized);
        TP t4 = now();

        // ── Step 5: Greedy mesh ────────────────────────────────────────────
        GreedyMesher::Config meshCfg;
        meshCfg.verbose = true;
        GreedyMesher mesher(meshCfg);
        auto quads = mesher.mesh(grid);
        TP t5 = now();

        if (quads.empty()) {
            std::cerr << "Error: no quads generated.\n";
            pauseConsole();
            return 1;
        }

        // ── Step 6: Build texture atlas + Minecraft model ──────────────────
        TextureAtlas atlas(0, 8192);
        McModel model(texPath);
        model.build(quads, grid, normalized, atlas, args.density);
        TP t6 = now();
        model.printStats();

        // ── Step 7: Write outputs ──────────────────────────────────────────
        atlas.writePng(pngOut);
        TP t7 = now();

        model.writeJson(jsonOut);
        TP t8 = now();

        std::cout << "\nDone!\n"
                << "  JSON: " << jsonOut << "\n"
                << "  PNG:  " << pngOut << "\n"
                << "\nNext steps:\n"
                << "  1. Copy " << args.modelName << ".json to:\n"
                << "       assets/" << args.modId << "/models/item/\n"
                << "  2. Copy " << args.modelName << ".png to:\n"
                << "       assets/" << args.modId << "/textures/item/\n";

        // ── Timing summary ─────────────────────────────────────────────────
        std::cout << "\n[Timing]\n"
                << "  Load:        " << elapsed(t0, t1) << "\n"
                << "  Normalize:   " << elapsed(t2, t3) << "\n"
                << "  Voxelize:    " << elapsed(t3, t4) << "\n"
                << "  Greedy mesh: " << elapsed(t4, t5) << "\n"
                << "  Bake atlas:  " << elapsed(t5, t6) << "\n"
                << "  Write PNG:   " << elapsed(t6, t7) << "\n"
                << "  Write JSON:  " << elapsed(t7, t8) << "\n"
                << "  ──────────────────────\n"
                << "  Total:       " << elapsed(t0, t8) << "\n";

        pauseConsole();
    } catch (const std::exception &e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
        pauseConsole();
        return 1;
    }
    return 0;
}
