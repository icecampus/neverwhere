#pragma once

#include <render_core/world_renderer.h>

class MapModel;
class AssetsLibraryModel;

// Bridge between the editor's Qt models and the plain render data of the
// shared WorldRenderer (the same renderer the game client uses).
namespace map_frame_bridge {

// GUI thread: snapshot the Qt map models into plain render data.
// Layer order matches the game client: BaseLandscape tiles first,
// then Decoration/GameplayInteractive sprites on top.
void buildWorldFrame(MapModel& mapModel, render_core::WorldFrame& outFrame);

// Render thread (GL active): upload GPU textures for every asset referenced
// by the frame (cached by uuid) and refresh live per-asset params (pivot/width).
void ensureFrameAssets(AssetsLibraryModel& assetsLibrary, const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer);

} // namespace map_frame_bridge
