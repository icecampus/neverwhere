#include "render_core/world_renderer.h"

#include <algorithm>

namespace render_core {

void WorldRenderer::init() {
    landscapeRenderer.init();
    spriteRenderer.init();
    overlayRenderer.init();
}

void WorldRenderer::shutdown() {
    overlayRenderer.shutdown();
    spriteRenderer.shutdown();
    landscapeRenderer.shutdown();
}

void WorldRenderer::ensureLandscapeAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows) {
    landscapeRenderer.ensureAtlas(assetUuid, atlasPath, cols, rows);
}

void WorldRenderer::ensureSpriteImage(const std::string& assetUuid, const std::filesystem::path& imagePath, float widthCells, const glm::vec2& pivot) {
    spriteRenderer.ensureImage(assetUuid, imagePath, widthCells, pivot);
}

void WorldRenderer::appendCellDiamond(std::vector<LineSegment>& lines, const glm::vec2& center, const glm::vec2& halfSize, const glm::vec4& color) const {
    // Same 4 lines as the editor's DiamondGrid cell: Left -> Up -> Right -> Down -> Left.
    const glm::vec2 left  = center + glm::vec2(-halfSize.x, 0.0f);
    const glm::vec2 up    = center + glm::vec2(0.0f, -halfSize.y);
    const glm::vec2 right = center + glm::vec2(halfSize.x, 0.0f);
    const glm::vec2 down  = center + glm::vec2(0.0f, halfSize.y);

    lines.push_back({left, up, color});
    lines.push_back({up, right, color});
    lines.push_back({right, down, color});
    lines.push_back({down, left, color});
}

void WorldRenderer::render(
    const WorldFrame& frame,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight) {

    landscapeRenderer.render(frame.landscapeTiles, iso, camera, viewWidth, viewHeight);
    spriteRenderer.render(frame.sprites, iso, camera, viewWidth, viewHeight);

    scratchLines.clear();

    if (frame.showGrid && viewWidth > 0 && viewHeight > 0 && camera.zoom > 0.0f) {
        // Editor parity (DiamondGrid): cell AABB of the viewport, capped —
        // a wildly zoomed-out view would otherwise produce millions of lines.
        const glm::vec2 viewSize((float)viewWidth, (float)viewHeight);
        const topology_core::CellRegion region = iso.visibleCellBounds(viewSize / camera.zoom, -camera.offset / camera.zoom);

        constexpr int kMaxCellsPerAxis = 256;
        const int cellsX = std::min(region.max.x - region.min.x + 1, kMaxCellsPerAxis);
        const int cellsY = std::min(region.max.y - region.min.y + 1, kMaxCellsPerAxis);

        const glm::vec2 cellSize = iso.dims.cellSize();
        const glm::vec2 halfSizeScreen = cellSize * 0.5f * camera.zoom;

        for (int i = 0; i < cellsX; ++i) {
            const int cx = region.min.x + i;
            for (int j = 0; j < cellsY; ++j) {
                const int cy = region.min.y + j;
                const glm::vec2 screenCenter = camera.worldToScreen(iso.mapToField({cx, cy}));
                appendCellDiamond(scratchLines, screenCenter, halfSizeScreen, frame.gridColor);
            }
        }
    }

    if (frame.cursorCell) {
        const glm::vec2 cellSize = iso.dims.cellSize();
        const glm::vec2 halfSizeScreen = cellSize * 0.5f * camera.zoom;
        const glm::vec2 screenCenter = camera.worldToScreen(iso.mapToField(*frame.cursorCell));
        appendCellDiamond(scratchLines, screenCenter, halfSizeScreen, frame.cursorColor);
    }

    overlayRenderer.render(scratchLines, viewWidth, viewHeight);
}

} // namespace render_core
