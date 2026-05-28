#include "RectangleCliffScenario.h"

#include "MeshBridge.h"
#include "PlaygroundState.h"

#include <cstddef>
#include <mutex>
#include <utility>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

void addBoundarySegment(RectangleCliffModel& model, int x, int y, BoundarySide side) {
    BoundarySegment segment;
    segment.side = side;

    switch (side) {
    case BoundarySide::Top:
        segment.a = {x, y};
        segment.b = {x + 1, y};
        break;
    case BoundarySide::Right:
        segment.a = {x + 1, y};
        segment.b = {x + 1, y + 1};
        break;
    case BoundarySide::Bottom:
        segment.a = {x + 1, y + 1};
        segment.b = {x, y + 1};
        break;
    case BoundarySide::Left:
        segment.a = {x, y + 1};
        segment.b = {x, y};
        break;
    }

    model.boundarySegments.push_back(segment);
}

void addMeshQuad(RectangleCliffModel& model, MeshQuad quad) {
    quad.depth = meshQuadDepth(quad);
    model.meshQuads.push_back(quad);
    if (quad.cliffWall) {
        model.cliffWallQuadCount++;
    } else {
        model.topQuadCount++;
    }
}

} // namespace

void sanitizeSettings(RectangleCliffSettings& settings) {
    settings.gridWidth = clampInt(settings.gridWidth, 4, 48);
    settings.gridHeight = clampInt(settings.gridHeight, 4, 32);
    settings.rectWidth = clampInt(settings.rectWidth, 1, settings.gridWidth);
    settings.rectHeight = clampInt(settings.rectHeight, 1, settings.gridHeight);
    settings.rectX = clampInt(settings.rectX, 0, settings.gridWidth - settings.rectWidth);
    settings.rectY = clampInt(settings.rectY, 0, settings.gridHeight - settings.rectHeight);
    settings.cutoutWidth = clampInt(settings.cutoutWidth, 1, settings.gridWidth);
    settings.cutoutHeight = clampInt(settings.cutoutHeight, 1, settings.gridHeight);
    settings.cutoutX = clampInt(settings.cutoutX, 0, settings.gridWidth - settings.cutoutWidth);
    settings.cutoutY = clampInt(settings.cutoutY, 0, settings.gridHeight - settings.cutoutHeight);
    settings.cliffHeight = clampFloat(settings.cliffHeight, 0.25f, 8.0f);
    settings.cornerBevel = clampFloat(settings.cornerBevel, 0.0f, 0.45f);
    settings.rockScale = clampFloat(settings.rockScale, 0.25f, 24.0f);
    settings.rockAmplitude = clampFloat(settings.rockAmplitude, 0.0f, 1.25f);
    settings.wallHorizontalSubdivisions = clampInt(settings.wallHorizontalSubdivisions, 1, 16);
    settings.wallVerticalSubdivisions = clampInt(settings.wallVerticalSubdivisions, 1, 16);
    settings.terraceSteps = clampInt(settings.terraceSteps, 0, 12);
}

bool isSolidCell(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y) {
    if (x < 0 || y < 0 || x >= settings.gridWidth || y >= settings.gridHeight) {
        return false;
    }

    return model.solidCells[cellIndex(x, y, settings.gridWidth)] != 0;
}

VertexKind classifyVertex(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y) {
    const bool topLeft = isSolidCell(model, settings, x - 1, y - 1);
    const bool topRight = isSolidCell(model, settings, x, y - 1);
    const bool bottomLeft = isSolidCell(model, settings, x - 1, y);
    const bool bottomRight = isSolidCell(model, settings, x, y);
    const int solidCount = (topLeft ? 1 : 0) + (topRight ? 1 : 0) + (bottomLeft ? 1 : 0) + (bottomRight ? 1 : 0);

    if (solidCount == 0) return VertexKind::Empty;
    if (solidCount == 1) return VertexKind::OuterCorner;
    if (solidCount == 3) return VertexKind::InnerCorner;
    if (solidCount == 4) return VertexKind::SolidInterior;

    const bool diagonal = (topLeft && bottomRight) || (topRight && bottomLeft);
    return diagonal ? VertexKind::DiagonalJoin : VertexKind::Edge;
}

void rebuildRectangleCliffModel() {
    spdlog::info("rebuildRectangleCliffModel: start");

    RectangleCliffSettings settings = g_rectSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rectSettings = settings;
    }

    RectangleCliffModel model;
    model.solidCells.assign((std::size_t)settings.gridWidth * (std::size_t)settings.gridHeight, 0);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const bool inMainRect = pointInRect(x, y, settings.rectX, settings.rectY, settings.rectWidth, settings.rectHeight);
            const bool inCutout = settings.enableCutout &&
                pointInRect(x, y, settings.cutoutX, settings.cutoutY, settings.cutoutWidth, settings.cutoutHeight);
            if (inMainRect && !inCutout) {
                model.solidCells[cellIndex(x, y, settings.gridWidth)] = 1;
                model.solidCellCount++;
            }
        }
    }

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            if (!isSolidCell(model, settings, x, y)) {
                continue;
            }

            if (!isSolidCell(model, settings, x, y - 1)) addBoundarySegment(model, x, y, BoundarySide::Top);
            if (!isSolidCell(model, settings, x + 1, y)) addBoundarySegment(model, x, y, BoundarySide::Right);
            if (!isSolidCell(model, settings, x, y + 1)) addBoundarySegment(model, x, y, BoundarySide::Bottom);
            if (!isSolidCell(model, settings, x - 1, y)) addBoundarySegment(model, x, y, BoundarySide::Left);
        }
    }

    for (int y = 0; y <= settings.gridHeight; y++) {
        for (int x = 0; x <= settings.gridWidth; x++) {
            const VertexKind kind = classifyVertex(model, settings, x, y);
            if (kind == VertexKind::Empty || kind == VertexKind::SolidInterior) {
                continue;
            }

            model.vertexMarkers.push_back({{x, y}, kind});
            switch (kind) {
            case VertexKind::Edge:
                model.edgeVertexCount++;
                break;
            case VertexKind::OuterCorner:
                model.outerCornerCount++;
                break;
            case VertexKind::InnerCorner:
                model.innerCornerCount++;
                break;
            case VertexKind::DiagonalJoin:
                model.diagonalJoinCount++;
                break;
            case VertexKind::Empty:
            case VertexKind::SolidInterior:
                break;
            }
        }
    }

    const landscape_mesh::SolidMaskGrid sharedMask = toSharedSolidMaskGrid(model.solidCells, settings.gridWidth, settings.gridHeight);
    landscape_mesh::SolidMeshBuildRequest meshRequest;
    meshRequest.mask = sharedMask;
    meshRequest.baseHeight = 0.0f;
    meshRequest.topHeight = settings.cliffHeight;
    meshRequest.level = 1;
    meshRequest.maxLevel = 1;
    meshRequest.includeWalls = true;
    meshRequest.fadeWallDisplacementAtBottom = false;

    const landscape_mesh::CompositionResult meshResult = landscape_mesh::composeSolidMaskMesh(
        meshRequest,
        makeSharedLandscapeMeshSettings(settings.cliffHeight));
    model.beveledSegmentCount = meshResult.stats.beveledSegmentCount;
    model.cornerCapCount = meshResult.stats.cornerCapCount;
    for (const landscape_mesh::MeshQuad& quad : meshResult.quads) {
        addMeshQuad(model, toAppMeshQuad(quad));
    }

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rectModel = std::move(model);
    }

    spdlog::info("rebuildRectangleCliffModel: done, solidCells={}, boundarySegments={}, beveledSegments={}, cornerCaps={}, vertices E={}/O={}/I={}/D={}",
        g_rectModel.solidCellCount,
        g_rectModel.boundarySegments.size(),
        g_rectModel.beveledSegmentCount,
        g_rectModel.cornerCapCount,
        g_rectModel.edgeVertexCount,
        g_rectModel.outerCornerCount,
        g_rectModel.innerCornerCount,
        g_rectModel.diagonalJoinCount);
}

} // namespace meshgen_playground
