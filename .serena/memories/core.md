# Neverwhere — Core

**Project**: 2D engine/suite for system-driven games (life-sim, narrative casual).
**Two main apps**: EpicMapEditor (Qt/QML) и EpicGameClient (standalone sokol_app + ImGui).
**Build**: CMake 3.20+, vcpkg (submodule at `toolchain/vcpkg`), Visual Studio 2022.
**Language**: C++20, Qt6 (QML/Quick/Widgets).

## Source layout
- `src/libs/` — shared libraries:
  - `core/` — editor core (Qt models, map, assets library, tools, topology)
  - `graphics/` — render core (Sokol GFX API)
  - `game_data/` — pure data layer (no Qt): JSON schemas, map/assets types
  - `game_runtime/` — runtime logic
  - `landscape_core/`, `landscape_mesh/` — landscape generation
  - `topology_core/` — topology/isometry math
  - `render_core/` — rendering utilities
  - `math/`, `containers/`, `utils/`, `generators/`, `ui/`, `base_data/`
  - `tests/` — unit tests
- `src/apps/` — applications:
  - `EpicMapEditor/` — main editor (Qt/QML)
  - `EpicGameClient/` — game client (sokol_app + ImGui, общий WorldRenderer)
  - `EcsPlayground/`, `RttrPlayground/`, `MeshGenerationPlayground/`, `Landscape3dPlayground/`, `SplattingPlayground/` — prototypes/experiments
- `src/refs/` — reference implementations (AParis69 volumetric terrains, rock fracturing)
- `vcpkg_overlays/ports/` — custom vcpkg overlay ports (sokol+imgui compat)

## Architecture
- ECS via EnTT, reflection via RTTR, serialization via nlohmann::json
- Rendering: Sokol GFX (`sokol_gfx`) shared core, different shells:
  - Editor: QtQuick/FBO (`QQuickFramebufferObject`)
  - Client: standalone (`sokol_app`) + ImGui
- CMake macros: `nw_add_qml_app(...)` for QML apps, `nw_add_lib_sources(...)` for libs
- PCH: `pch.h` in each lib/app

## Key deps
Qt6 (qml, quick, widgets, quickcontrols2, shader tools, core5compat), Boost (uuid, lexical_cast, container_hash), magic_enum, glm, nlohmann_json, spdlog, EnTT, RTTR, fastnoise2, libnoise, sokol, imgui, stb, glad, gtest

For tech stack details see `mem:tech_stack`.