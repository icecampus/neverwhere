#include "BatchExport.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

// Batch path uses upstream XxxScene() wrappers (hardcoded MC + OBJ names).
// Viewer path uses Build*TerrainTree() from ivt_scenes.h via IvtScene.
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
    spdlog::info("  --smoke-test runs all upstream batch scenes (island/karst/sea); sea@350^3 may take several minutes.");
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

struct SmokeCase {
    const char* label;
    const char* sceneArg;
    const char* objName;
};

bool runSmokeCaseIsolated(const SmokeCase& test, const std::filesystem::path& exePath) {
    spdlog::info("IVT smoke: {}", test.label);
    const auto outPath = std::filesystem::current_path() / test.objName;

    std::error_code ec;
    std::filesystem::remove(outPath, ec);

    const std::string cmd = "\"" + exePath.string() + "\" --batch-export --scene " + test.sceneArg;
    const int exitCode = std::system(cmd.c_str());
    if (exitCode != 0) {
        spdlog::error("TEST FAIL: {} subprocess exit code {}", test.label, exitCode);
        return false;
    }

    if (!objLooksValid(outPath)) {
        spdlog::error("TEST FAIL: missing or empty {} at {}", test.objName, outPath.string());
        return false;
    }

    spdlog::info("TEST PASS: {} ({} bytes)", test.objName, std::filesystem::file_size(outPath));
    return true;
}

bool runSmokeTest(const char* argv0) {
    spdlog::info("IVT smoke test: all upstream batch scenes (isolated subprocess per scene)");
    const std::filesystem::path exePath = std::filesystem::absolute(argv0);
    const SmokeCase cases[] = {
        {"FloatingIsland (MC 100^3)", "island", "islands.obj"},
        {"KarstScene (MC 200^3)", "karst", "karst.obj"},
        {"SeaScene (MC 350^3)", "sea", "sea.obj"},
    };

    bool allOk = true;
    for (const SmokeCase& test : cases) {
        if (!runSmokeCaseIsolated(test, exePath)) {
            allOk = false;
        }
    }

    if (allOk) {
        spdlog::info("TEST PASS: islands.obj, karst.obj, sea.obj");
    } else {
        spdlog::error("TEST FAIL: one or more batch scenes failed");
    }
    return allOk;
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

    // Upstream parity: fixed seed for deterministic batch OBJ export.
    std::srand(1234);

    if (smokeTest) {
        return runSmokeTest(argv[0]) ? 0 : 1;
    }

    spdlog::info("aparis69-implicit-volumetric-terrains-ref: cwd={}", std::filesystem::current_path().string());
    runScene(scene);
    spdlog::info("Done.");
    return 0;
}
