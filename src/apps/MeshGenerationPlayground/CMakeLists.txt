find_package(imgui CONFIG REQUIRED)
find_package(FastNoise2 CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        FastNoise2::FastNoise
        landscape_core
        landscape_mesh
)

if(WIN32)
    add_custom_command(TARGET ${AppName} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FastNoise2::FastNoise>
            $<TARGET_FILE_DIR:${AppName}>/$<TARGET_FILE_NAME:FastNoise2::FastNoise>
        VERBATIM
    )
endif()

# Sokol is header-only in this project (via vcpkg overlay).
find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

# Sokol backend link requirements (standalone app uses sokol_app + D3D11 on Windows).
if(MSVC)
    target_link_libraries(${AppName} PRIVATE d3d11 dxgi)
endif()

