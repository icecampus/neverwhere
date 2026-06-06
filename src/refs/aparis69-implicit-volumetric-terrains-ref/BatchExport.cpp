#include "BatchExport.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

void SeaScene();
void KarstScene();
void FloatingIsland();

namespace {

enum class Scene {
    All,
    Sea,
    Karst,
    Island,
};

void printUsage(const char* argv0) {
    spdlog::info("Usage: {} [--batch-export] [--scene sea|karst|island|all] [--smoke-test]", argv0);
    spdlog::info("  Exports OBJ meshes to the current working directory (sea.obj, karst.obj, islands.obj).");
}

Scene parseScene(const char* name) {
    if (std::strcmp(name, "sea") == 0) return Scene::Sea;
    if (std::strcmp(name, "karst") == 0) return Scene::Karst;
    if (std::strcmp(name, "island") == 0) return Scene::Island;
    if (std::strcmp(name, "all") == 0) return Scene::All;
    return Scene::All;
}

void runScene(Scene scene) {
    switch (scene) {
    case Scene::Sea:
        spdlog::info("Running SeaScene (MC 350^3, output sea.obj)...");
        SeaScene();
        break;
    case Scene::Karst:
        spdlog::info("Running KarstScene (MC 200^3, output karst.obj)...");
        KarstScene();
        break;
    case Scene::Island:
        spdlog::info("Running FloatingIsland (MC 100^3, output islands.obj)...");
        FloatingIsland();
        break;
    case Scene::All:
        spdlog::info("Running all upstream demo scenes...");
        SeaScene();
        FloatingIsland();
        KarstScene();
        break;
    }
}

bool objLooksValid(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    return std::filesystem::file_size(path, ec) > 64 && !ec;
}

bool runSmokeTest() {
    spdlog::info("IVT smoke test: FloatingIsland only");
    const auto cwd = std::filesystem::current_path();
    const auto outPath = cwd / "islands.obj";

    std::error_code ec;
    std::filesystem::remove(outPath, ec);

    FloatingIsland();

    if (!objLooksValid(outPath)) {
        spdlog::error("TEST FAIL: missing or empty islands.obj at {}", outPath.string());
        return false;
    }

    spdlog::info("TEST PASS: islands.obj exported ({} bytes)", std::filesystem::file_size(outPath));
    return true;
}

} // namespace

int runIvtBatchExport(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);

    bool smokeTest = false;
    Scene scene = Scene::All;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTest = true;
            continue;
        }
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            scene = parseScene(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--batch-export") == 0) {
            continue;
        }
        spdlog::warn("Unknown argument: {}", argv[i]);
    }

    std::srand(1234);

    if (smokeTest) {
        return runSmokeTest() ? 0 : 1;
    }

    spdlog::info("aparis69-implicit-volumetric-terrains-ref: cwd={}", std::filesystem::current_path().string());
    runScene(scene);
    spdlog::info("Done.");
    return 0;
}
