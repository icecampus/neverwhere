find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

nw_add_lib_sources(
    NAME game_runtime
    GROUP libs/common
    SOURCES
        pch.h
        include/game_runtime/game_types.h
        include/game_runtime/fixture.h
        src/fixture.cpp
        include/game_runtime/game_world.h
        src/game_world.cpp
        include/game_runtime/game_session.h
        src/game_session.cpp
        include/game_runtime/editor_extensions.h
        src/editor_extensions.cpp
        include/game_runtime/runtime.h
        src/runtime.cpp
        include/game_runtime/lib.h
    LIBS
        game_data
        nlohmann_json::nlohmann_json
        glm::glm-header-only
        spdlog::spdlog_header_only
)

target_include_directories(game_runtime PUBLIC include)

# Enable C++20
target_compile_features(game_runtime PUBLIC cxx_std_20)
