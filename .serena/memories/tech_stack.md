# Tech Stack

- **Language**: C++20 (MSVC, Clang for Emscripten)
- **Build**: CMake 3.20+, vcpkg (submodule), presets: `vs2022`, `emscripten`
- **UI Editor**: Qt6 (QML/Quick/Widgets), QQuickFramebufferObject for render embedding
- **UI Runtime**: Dear ImGui
- **ECS**: EnTT
- **Reflection**: RTTR (Run Time Type Reflection) — auto-inspector, auto-serialization
- **Serialization**: nlohmann::json
- **Rendering**: Sokol GFX (`sokol_gfx`), glad (GL loader on Windows)
- **Math**: glm
- **Logging**: spdlog (non-emscripten)
- **Noise**: fastnoise2, libnoise
- **Undo/Redo (planned)**: immer (persistent data structures)
- **Scripting (planned)**: Lua via Sol2
- **Testing**: gtest

## Build commands
- Generate VS solution: `generate_vs.bat` or `cmake --preset vs2022`
- Build: `cmake --build --preset debug --target <Target>`
- Build artifacts: `_intermediate_64/` (VS), `_b-em/` (Emscripten)