#include "pch.h"

#include "PlaygroundSmokeTest.h"

#include <cmath>

#include <spdlog/spdlog.h>

#include "LandscapeCellCatalog.h"
#include "LandscapeModel.h"

namespace landscape3d {

namespace {

bool nearlyEqual(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= 0.0001f;
}

bool validateCatalog(const LandscapeCellCatalog& catalog) {
    if (!catalog.valid()) {
        spdlog::error("TEST FAIL Landscape3d catalog: one or more templates are empty");
        return false;
    }

    const float highground = catalog.settings().highgroundHeight;
    for (int value = 0; value < 16; ++value) {
        const auto type = static_cast<landscape_core::LandscapeTileType>(value);
        const LandscapeCellTemplate& cellTemplate = catalog.templateFor(type);
        if (landscape_core::nodeMaskToTileType(cellTemplate.nodeMask) != type ||
            cellTemplate.quads.empty()) {
            spdlog::error("TEST FAIL Landscape3d catalog: invalid template {}", value);
            return false;
        }

        for (const landscape_mesh::MeshQuad& quad : cellTemplate.quads) {
            const std::array<float, 4> heights{quad.a.y, quad.b.y, quad.c.y, quad.d.y};
            for (const float height : heights) {
                if (height < -0.0001f || height > highground + 0.0001f) {
                    spdlog::error("TEST FAIL Landscape3d catalog: out-of-range height in template {}", value);
                    return false;
                }
            }
            if (!quad.cliffWall &&
                (!nearlyEqual(quad.a.y, quad.b.y) ||
                    !nearlyEqual(quad.b.y, quad.c.y) ||
                    !nearlyEqual(quad.c.y, quad.d.y) ||
                    (!nearlyEqual(quad.a.y, 0.0f) && !nearlyEqual(quad.a.y, highground)))) {
                spdlog::error("TEST FAIL Landscape3d catalog: non-planar top in template {}", value);
                return false;
            }
            if (quad.cliffWall && nearlyEqual(quad.a.y, quad.b.y) &&
                nearlyEqual(quad.b.y, quad.c.y) && nearlyEqual(quad.c.y, quad.d.y)) {
                spdlog::error("TEST FAIL Landscape3d catalog: flat wall panel in template {}", value);
                return false;
            }
        }
    }
    return true;
}

bool validateBrushContract() {
    LandscapeModel model;
    model.reset(8, 8);

    const GridPoint nodeA{3, 3};
    const GridPoint nodeB{4, 3};
    if (!model.setNodeHigh(nodeA, true) || model.highNodeCount() != 1) {
        return false;
    }
    for (const GridPoint cell : model.affectedCells(nodeA)) {
        const auto type = model.cellTypeAt(cell);
        if (type == landscape_core::LandscapeTileType::Unknown ||
            !landscape_core::tileTypeHasSurface(type)) {
            return false;
        }
    }

    if (!model.setNodeHigh(nodeB, true) ||
        model.cellTypeAt({3, 3}) != landscape_core::LandscapeTileType::RightUpLine) {
        return false;
    }

    if (!model.setNodeHigh(nodeA, false) || !model.setNodeHigh(nodeB, false)) {
        return false;
    }
    return model.highNodeCount() == 0 &&
        model.cellTypeCount(landscape_core::LandscapeTileType::Unknown) == model.width() * model.height();
}

} // namespace

bool runTestScenario(const LandscapeCellCatalog& catalog) {
    const bool catalogOk = validateCatalog(catalog);
    const bool brushOk = validateBrushContract();
    const bool passed = catalogOk && brushOk;
    spdlog::info(
        "{} Landscape3d binary painter smoke: templates={}, catalog={}, brush={}",
        passed ? "TEST PASS" : "TEST FAIL",
        16,
        catalogOk,
        brushOk);
    return passed;
}

} // namespace landscape3d
