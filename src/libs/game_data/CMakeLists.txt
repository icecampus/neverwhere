find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

nw_add_lib_sources(
    NAME game_data
    GROUP libs
    SOURCES
        pch.h
        include/game_data/types.h
        include/game_data/map.h
        src/map.cpp
        include/game_data/assets.h
        src/assets.cpp
    LIBS
        nlohmann_json::nlohmann_json
        magic_enum::magic_enum
        glm::glm-header-only
)

target_include_directories(game_data PUBLIC include)

