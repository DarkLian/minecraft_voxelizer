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
    std::string outputDir = "./output";
    std::string modelName;
    std::string modId = "mymod";
    int quality = 3;
    bool solidFill = false;
    bool help = false;
};

static void printUsage() {
    std::cout <<
            "Usage: mc_voxelizer <input.obj|.gltf|.glb> [options]\n"
            "\n"
            "Options:\n"
            "  --quality  1-5    Voxel resolution quality (default: 3)\n"
            "                      1 = 16³  (fastest, blockiest)\n"
            "                      2 = 24³\n"
            "                      3 = 32³  (recommended)\n"
            "                      4 = 48³\n"
            "                      5 = 64³  (slowest, finest detail)\n"
            "  --output   <dir>  Output directory (default: ./output)\n"
            "  --name     <str>  Model/file name (default: input filename stem)\n"
            "  --modid    <str>  Mod namespace for texture path (default: mymod)\n"
            "  --solid           Fill solid interior voxels (default: off)\n"
            "  --help            Show this help\n"
            "\n"
            "Examples:\n"
            "  mc_voxelizer sword.obj\n"
            "  mc_voxelizer dragon.gltf --quality 4 --modid darkaddons --name dragon\n"
            "  mc_voxelizer ship.obj --quality 5 --solid --output ./assets\n"
            "\n"
            "Output:\n"
            "  <output>/<name>.json   Minecraft model file\n"
            "  <output>/<name>.png    Texture atlas\n";
}

static CliArgs parseArgs(const int argc, char **argv) {
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
        } else if (arg == "--output" && i + 1 < argc) {
            args.outputDir = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            args.modelName = argv[++i];
        } else if (arg == "--modid" && i + 1 < argc) {
            args.modId = argv[++i];
        } else if (arg == "--solid") {
            args.solidFill = true;
        } else if (arg[0] != '-') {
            // Positional argument: input file
            args.inputPath = arg;
        } else {
            throw std::invalid_argument("Unknown option: " + arg);
        }
    }

    if (args.inputPath.empty())
        throw std::invalid_argument("No input file specified.");

    // Derive model name from filename stem if not set
    if (args.modelName.empty()) {
        args.modelName =
                std::filesystem::path(args.inputPath).stem().string();
    }

    return args;
}

static CliArgs interactivePrompt() {
    CliArgs args;
    std::cout << "--- Interactive Mode ---\n";
    std::cout << "Drag and drop your 3D model file here (or type the path), then press Enter:\n> ";
    std::string path;
    std::getline(std::cin, path);

    // Clean up Windows drag-and-drop quotes
    if (!path.empty() && path.front() == '"' && path.back() == '"') {
        path = path.substr(1, path.size() - 2);
    }
    args.inputPath = path;

    if (args.inputPath.empty()) {
        throw std::invalid_argument("No input file specified.");
    }

    std::cout << "Enter voxel resolution quality (1-5) [Press Enter for default 3]:\n> ";
    std::string q;
    std::getline(std::cin, q);
    if (!q.empty()) {
        args.quality = std::stoi(q);
        if (args.quality < 1 || args.quality > 5) {
            throw std::invalid_argument("--quality must be 1-5.");
        }
    }

    std::cout << "Enable solid interior fill? (y/n) [Press Enter for default n]:\n> ";
    std::string s;
    std::getline(std::cin, s);
    if (s == "y" || s == "Y") {
        args.solidFill = true;
    }

    // --- NEW: Mod ID Prompt ---
    std::cout << "Enter mod ID [Press Enter for default 'darkaddons']:\n> ";
    std::string m;
    std::getline(std::cin, m);
    if (!m.empty()) {
        args.modId = m;
    } else {
        args.modId = "darkaddons"; // Automatically assumes your mod!
    }

    // --- NEW: Model Name Prompt ---
    std::cout << "Enter model name [Press Enter to use filename]:\n> ";
    std::string n;
    std::getline(std::cin, n);
    if (!n.empty()) {
        args.modelName = n;
    } else {
        // Fallback to deriving from filename if they just press Enter
        args.modelName = std::filesystem::path(args.inputPath).stem().string();
    }

    std::cout << "\nStarting generation...\n";
    return args;
}

// A helper to stop the window from closing instantly
static void pauseConsole() {
    std::cout << "\nPress Enter to exit...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main pipeline
// ─────────────────────────────────────────────────────────────────────────────
int main(const int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "╔══════════════════════════════════════╗\n"
            << "║   Minecraft Voxelizer  v1.0.0        ║\n"
            << "╚══════════════════════════════════════╝\n\n";

    CliArgs args;
    try {
        // IF double-clicked (no arguments), run interactive mode!
        if (argc < 2) {
            args = interactivePrompt();
        } else {
            // Otherwise, read the terminal commands normally
            args = parseArgs(argc, argv);
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        printUsage();
        pauseConsole(); // Pause on error
        return 1;
    }

    if (args.help) {
        printUsage();
        return 0;
    }

    // Ensure output directory exists
    std::filesystem::create_directories(args.outputDir);

    std::string jsonOut = args.outputDir + "/" + args.modelName + ".json";
    std::string pngOut = args.outputDir + "/" + args.modelName + ".png";
    std::string texPath = args.modId + ":item/" + args.modelName;

    std::cout << "Input:    " << args.inputPath << "\n"
            << "Output:   " << args.outputDir << "\n"
            << "Name:     " << args.modelName << "\n"
            << "Quality:  " << args.quality
            << " (" << Voxelizer::qualityToResolution(args.quality) << "³)\n"
            << "Texture:  " << texPath << "\n\n";

    try {
        // ── Step 1: Load mesh ──────────────────────────────────────────────────
        auto loader = MeshLoader::create(args.inputPath);
        Mesh mesh = loader->load(args.inputPath);

        // ── Step 2: Normalize to MC space ──────────────────────────────────────
        Normalizer::Config normCfg;
        normCfg.snapFloor = true;
        Normalizer normalizer(normCfg);
        Mesh normalized = normalizer.normalize(mesh);

        // ── Step 3: Voxelize ───────────────────────────────────────────────────
        Voxelizer::Config voxCfg;
        voxCfg.quality = args.quality;
        voxCfg.solidFill = args.solidFill;
        voxCfg.verbose = true;
        Voxelizer voxelizer(voxCfg);
        VoxelGrid grid = voxelizer.voxelize(normalized);

        // ── Step 4: Greedy mesh ────────────────────────────────────────────────
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

        // ── Step 5: Build texture atlas ────────────────────────────────────────
        TextureAtlas atlas;

        // ── Step 6: Build Minecraft model ──────────────────────────────────────
        McModel model(texPath);
        model.build(quads, atlas); // registers colors in atlas, computes UVs
        model.printStats();

        // ── Step 7: Write outputs ──────────────────────────────────────────────
        atlas.writePng(pngOut);
        model.writeJson(jsonOut);

        std::cout << "\n✓ Done!\n"
                << "  JSON: " << jsonOut << "\n"
                << "  PNG:  " << pngOut << "\n"
                << "\nNext steps:\n"
                << "  1. Copy " << args.modelName << ".json to:\n"
                << "       src/main/resources/assets/" << args.modId << "/models/item/\n"
                << "  2. Copy " << args.modelName << ".png to:\n"
                << "       src/main/resources/assets/" << args.modId << "/textures/item/\n"
                << "  3. Open the .json in Blockbench to adjust display transforms.\n";

        pauseConsole(); // Pause on success
    } catch (const std::exception &e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
        pauseConsole(); // Pause on fatal error
        return 1;
    }

    return 0;
}
