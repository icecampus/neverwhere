# PggViewer: read-only node-graph projection of .pgg files (spec §10, stage E8).
# Sibling tool of PggTool — a permanent viewer, not a one-off prototype.
if(EMSCRIPTEN)
    return()
endif()

find_package(imgui CONFIG REQUIRED)

nw_add_console_app(
    GROUP apps/tools
    LIBS
        pgg
        spdlog::spdlog_header_only
        imgui::imgui
)

find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

find_path(STB_INCLUDE_DIRS "stb_image_write.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image_write.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${STB_INCLUDE_DIRS})

nw_configure_sokol_app(${AppName})
