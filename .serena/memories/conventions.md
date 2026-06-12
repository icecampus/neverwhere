# Code Conventions

- PCH: each lib/app has `pch.h`; include it as first header
- CMake macros: `nw_add_qml_app(NAME ... LIBS ...)` for apps, `nw_add_lib_sources(...)` for libs
  - defined in `cmake/utils.cmake`
- Qt/QML + ECS: adapter pattern (`QAbstractListModel`) over EnTT registry
- Data layer (`game_data`) must NOT depend on Qt
- Sokol in QtQuick: `sg_reset_state_cache()` before draw, `sg_commit()` exactly once per frame, lazy `sg_setup()` only when GL context active
- Components use `RTTR_ENABLE()` macro for reflection registration
- Lib headers at `src/libs/<name>/` or `src/libs/<name>/include/<name>/`