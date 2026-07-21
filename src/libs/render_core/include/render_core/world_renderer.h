#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/landscape_renderer.h"
#include "render_core/overlay_renderer.h"
#include "render_core/sprite_renderer.h"

#include "topology_core/camera2d.h"
#include "topology_core/diamond_isometry.h"

namespace render_core {

// Plain render input for one frame — no Qt, no game data types, so any shell
// (game client, Qt editor, playground) can fill it from its own data source.
struct WorldFrame {
    std::vector<LandscapeTile> landscapeTiles;
    std::vector<LandscapeTile> raisedTiles; // RaisedLandscape layer (3D tiles: walls + lifted top)
    std::vector<SpriteInstance> sprites;

    bool showGrid = true;
    glm::vec4 gridColor{0.5f, 0.5f, 0.5f, 1.0f}; // QML "grey"

    std::optional<glm::ivec2> cursorCell;
    glm::vec4 cursorColor{1.0f, 0.0f, 0.0f, 1.0f}; // QML "red"
};

// Facade over the world renderers — the single world render shared by shells.
// Draw order matches the editor's MapView.qml: flat landscape tiles, raised
// landscape (cliff walls + lifted tops), Tile2D sprites, then overlays (grid,
// cell cursor) on top.
class WorldRenderer {
public:
    // depthFormat must match the pass the renderer draws into:
    // SG_PIXELFORMAT_DEPTH_STENCIL for the sokol_app swapchain (game client, default),
    // SG_PIXELFORMAT_NONE for a Qt FBO without a wrapped depth attachment (editor).
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    void ensureLandscapeAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows);
    void ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath = {});
    void ensureSpriteImage(const std::string& assetUuid, const std::filesystem::path& imagePath, float widthCells, const glm::vec2& pivot);

    void render(
        const WorldFrame& frame,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight);

private:
    LandscapeRenderer landscapeRenderer;
    SpriteRenderer spriteRenderer;
    OverlayRenderer overlayRenderer;

    std::vector<LineSegment> scratchLines;

    void appendCellDiamond(std::vector<LineSegment>& lines, const glm::vec2& screenCenter, const glm::vec2& halfSize, const glm::vec4& color) const;
};

} // namespace render_core
