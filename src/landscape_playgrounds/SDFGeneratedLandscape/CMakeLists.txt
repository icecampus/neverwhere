find_package(imgui CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        topology_core
        landscape_core
        highground_core
        stone_gen
)

if(WIN32)
    find_package(FastNoise2 CONFIG REQUIRED)
    add_custom_command(TARGET ${AppName} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FastNoise2::FastNoise>
            $<TARGET_FILE_DIR:${AppName}>/$<TARGET_FILE_NAME:FastNoise2::FastNoise>
        VERBATIM
    )
endif()

find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${STB_INCLUDE_DIRS})

nw_configure_sokol_app(${AppName})
if(APPLE AND SDFGL_FORCE_GLCORE)
    # Opt-in GLCORE build for --shot framebuffer readback on macOS
    # (the Metal default has no readback path; same pattern as StoneCube).
    target_compile_definitions(${AppName} PRIVATE SOKOL_GLCORE)
    target_link_libraries(${AppName} PRIVATE "-framework OpenGL")
endif()
if(MSVC)
    # dwmapi: DwmFlush in the Win32 screenshot helper.
    target_link_libraries(${AppName} PRIVATE dwmapi)
endif()
