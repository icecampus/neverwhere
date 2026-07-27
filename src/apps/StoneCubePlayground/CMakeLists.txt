find_package(imgui CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        stone_gen
)

# Sokol is header-only in this project (via vcpkg overlay).
find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

# stb headers (image writing)
find_path(STB_INCLUDE_DIRS "stb_image_write.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image_write.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${STB_INCLUDE_DIRS})

# Sokol backend link requirements (sokol_app: macOS ObjC++, Linux X11/GL).
nw_configure_sokol_app(${AppName})

# This playground is deliberately single-source GLSL with the GLCORE backend on
# every desktop platform (same exception as ShadertoyPlayground, see AGENTS.md),
# so it needs the GL libs that nw_configure_sokol_app only adds on Linux.
if(MSVC)
    target_link_libraries(${AppName} PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(${AppName} PRIVATE "-framework OpenGL")
endif()
