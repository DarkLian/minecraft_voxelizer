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

#ifdef _WIN32
#include <windows.h>
#endif

struct CliArgs {
    std::string inputPath;
    std::string outputDir  = "./output";
    std::string modelName;
    std::string modId      = "mymod";
    int quality            = 3;
    int density            = 1;   // pixels per voxel in the texture atlas
    bool solidFill         = false;
    bool help              = false;
};

static void printUsage() {
    std::cout <<
        "Usage: mc_voxelizer <input.obj|.gltf|.glb> [options]\n"
        "\n"
        "Options:\n"
        "  --quality  1-5    Voxel resolution (default: 3)\n"
        "                      1 = 16³  fastest / blockiest\n"
        "                      2 = 24³\n"
        "                      3 = 32³  recommended\n"
        "                      4 = 48³\n"
        "                      5 = 64³  finest detail\n"
        "  --density  1-16   Texture pixels per voxel (default: 1)\n"
        "                      1 = 1 px/voxel  (compact, MC-safe)\n"
        "                      2 = 2×2 px/voxel\n"
        "                      4 = 4×4 px/voxel  (good detail)\n"
        "                      8 = 8×8 px/voxel  (high detail)\n"
        "                    Higher values produce larger PNGs and richer\n"
        "                    colour detail without increasing element count.\n"
        "  --output   <dir>  Output directory (default: ./output)\n"
        "  --name     <str>  Model/file name (default: input filename stem)\n"
        "  --modid    <str>  Mod namespace for texture path (default: mymod)\n"
        "  --solid           Fill solid interior voxels (default: off)\n"
        "  --help            Show this help\n"
        "\n"
        "Examples:\n"
        "  mc_voxelizer sword.obj\n"
        "  mc_voxelizer dragon.gltf --quality 4 --density 4 --modid darkaddons --name dragon\n"
        "  mc_voxelizer ship.obj --quality 5 --density 8 --solid --output ./assets\n"
        "\n"
        "Output:\n"
        "  <output>/<name>.json   Minecraft model file\n"
        "  <output>/<name>.png    Texture atlas\n";
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
            if (args.quality < 1 || args.quality > 5)
                throw std::invalid_argument("--quality must be 1–5.");
        } else if (arg == "--density" && i + 1 < argc) {
            args.density = std::stoi(argv[++i]);
            if (args.density < 1 || args.density > 16)
                throw std::invalid_argument("--density must be 1–16.");
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
    std::cout << "Drag and drop your 3D model file here (or type the path), then press Enter:\n> ";
    std::string path;
    std::getline(std::cin, path);

    // Clean up Windows drag-and-drop quotes
    if (!path.empty() && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    args.inputPath = path;
    if (args.inputPath.empty())
        throw std::invalid_argument("No input file specified.");

    std::cout << "Enter voxel resolution quality (1-5) [Enter = 3]:\n> ";
    std::string q; std::getline(std::cin, q);
    if (!q.empty()) {
        args.quality = std::stoi(q);
        if (args.quality < 1 || args.quality > 5)
            throw std::invalid_argument("--quality must be 1-5.");
    }

    std::cout << "Enter texture pixel density (1/2/4/8/16) [Enter = 1]:\n"
              << "  Higher = richer colour detail, larger PNG file.\n> ";
    std::string d; std::getline(std::cin, d);
    if (!d.empty()) {
        args.density = std::stoi(d);
        if (args.density < 1 || args.density > 16)
            throw std::invalid_argument("--density must be 1-16.");
    }

    std::cout << "Enable solid interior fill? (y/n) [Enter = n]:\n> ";
    std::string s; std::getline(std::cin, s);
    if (s == "y" || s == "Y") args.solidFill = true;

    std::cout << "Enter mod ID [Enter = 'darkaddons']:\n> ";
    std::string m; std::getline(std::cin, m);
    args.modId = m.empty() ? "darkaddons" : m;

    std::cout << "Enter model name [Enter = use filename]:\n> ";
    std::string n; std::getline(std::cin, n);
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
// Main pipeline
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "╔══════════════════════════════════════╗\n"
              << "║   Minecraft Voxelizer  v1.1.0        ║\n"
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

    if (args.help) { printUsage(); return 0; }

    std::filesystem::create_directories(args.outputDir);

    std::string jsonOut = args.outputDir + "/" + args.modelName + ".json";
    std::string pngOut  = args.outputDir + "/" + args.modelName + ".png";
    std::string texPath = args.modId + ":item/" + args.modelName;

    std::cout << "Input:    " << args.inputPath << "\n"
              << "Output:   " << args.outputDir << "\n"
              << "Name:     " << args.modelName << "\n"
              << "Quality:  " << args.quality
              << " (" << Voxelizer::qualityToResolution(args.quality) << "³)\n"
              << "Density:  " << args.density << " px/voxel\n"
              << "Texture:  " << texPath << "\n\n";

    try {
        // ── Step 1: Load mesh ──────────────────────────────────────────────
        auto loader = MeshLoader::create(args.inputPath);
        Mesh mesh   = loader->load(args.inputPath);

        // ── Step 2: Normalize to MC space ──────────────────────────────────
        Normalizer::Config normCfg;
        normCfg.snapFloor = true;
        Normalizer normalizer(normCfg);
        Mesh normalized = normalizer.normalize(mesh);

        // ── Step 3: Voxelize ───────────────────────────────────────────────
        Voxelizer::Config voxCfg;
        voxCfg.quality   = args.quality;
        voxCfg.solidFill = args.solidFill;
        voxCfg.verbose   = true;
        Voxelizer voxelizer(voxCfg);
        VoxelGrid grid = voxelizer.voxelize(normalized);

        // ── Step 4: Greedy mesh (geometry-only) ───────────────────────────
        GreedyMesher::Config meshCfg;
        meshCfg.verbose = true;
        GreedyMesher mesher(meshCfg);
        auto quads = mesher.mesh(grid);

        if (quads.empty()) {
            std::cerr << "Error: no quads generated. "
                      << "The mesh may be too small for the chosen quality level, "
                      << "or the input file has geometry issues.\n";
            pauseConsole();
            return 1;
        }

        // ── Step 5: Build texture atlas + Minecraft model ──────────────────
        // The atlas strip width is capped at 4096 px; rows wrap automatically.
        TextureAtlas atlas(4096);
        McModel model(texPath);
        model.build(quads, grid, atlas, args.density);
        model.printStats();

        // ── Step 6: Write outputs ──────────────────────────────────────────
        atlas.writePng(pngOut);
        model.writeJson(jsonOut);

        std::cout << "\n✓ Done!\n"
                  << "  JSON: " << jsonOut << "\n"
                  << "  PNG:  " << pngOut  << "\n"
                  << "\nNext steps:\n"
                  << "  1. Copy " << args.modelName << ".json to:\n"
                  << "       src/main/resources/assets/" << args.modId << "/models/item/\n"
                  << "  2. Copy " << args.modelName << ".png to:\n"
                  << "       src/main/resources/assets/" << args.modId << "/textures/item/\n"
                  << "  3. Open the .json in Blockbench to adjust display transforms.\n";

        pauseConsole();
    } catch (const std::exception &e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
        pauseConsole();
        return 1;
    }

    return 0;
}