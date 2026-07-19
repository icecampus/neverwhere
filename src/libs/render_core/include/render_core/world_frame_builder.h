#pragma once

#include "render_core/world_renderer.h"

#include "game_data/assets.h"
#include "game_data/map.h"

namespace render_core {

// Shared world-frame construction (game client + editor play-test tab).
// Layer order: BaseLandscape tiles first, then Decoration/GameplayInteractive
// sprites on top.
void collectWorldFrame(const game_data::Map& map, WorldFrame& outFrame);

// Upload GPU textures for every asset referenced by the frame (cached by uuid).
void ensureWorldAssets(const game_data::AssetIndex& assetIndex, const WorldFrame& frame, WorldRenderer& renderer);

} // namespace render_core
