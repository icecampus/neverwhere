find_package(glm CONFIG REQUIRED)

nw_add_lib_sources(
    NAME stone_gen
    GROUP libs/common
    SOURCES
        pch.h
        include/stone_gen/stone_sdf.h
        include/stone_gen/stone_mesh.h
        include/stone_gen/stone_bake.h
        src/stone_sdf.cpp
        src/stone_mesh.cpp
        src/stone_bake.cpp
    LIBS
        glm::glm-header-only
        highground_core
)

target_include_directories(stone_gen PUBLIC include)
