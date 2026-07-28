find_package(glm CONFIG REQUIRED)

# Sokol headers (from vcpkg, header-only)
find_path(SOKOL_INCLUDE_DIRS "sokol_gfx.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_gfx.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()

# stb headers (from vcpkg)
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image.h). Make sure vcpkg dependency 'stb' is installed.")
endif()

nw_add_lib_sources(
    NAME render_core
    GROUP libs/common
    SOURCES
        pch.h
        include/render_core/sokol_config.h
        include/render_core/image_loader.h
        src/image_loader.cpp
        include/render_core/texture_atlas.h
        src/texture_atlas.cpp
        include/render_core/landscape_renderer.h
        src/landscape_renderer.cpp
        include/render_core/cliff_renderer.h
        src/cliff_renderer.cpp
        include/render_core/cyclopean_renderer.h
        src/cyclopean_renderer.cpp
        src/atlas_tile_types.h
        include/render_core/sprite_renderer.h
        src/sprite_renderer.cpp
        include/render_core/overlay_renderer.h
        src/overlay_renderer.cpp
        include/render_core/world_renderer.h
        src/world_renderer.cpp
        include/render_core/world_frame_builder.h
        src/world_frame_builder.cpp
        include/render_core/mesh_preview_renderer.h
        src/mesh_preview_renderer.cpp
    LIBS
        glm::glm-header-only
        topology_core
        landscape_core
        landscape_mesh
        highground_core
        game_data
        spdlog::spdlog_header_only
)

target_include_directories(render_core
    PUBLIC
        include
    PRIVATE
        ${SOKOL_INCLUDE_DIRS}
        ${STB_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(render_core PRIVATE d3d11)
endif()

