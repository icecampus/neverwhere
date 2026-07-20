find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        glm::glm-header-only
        landscape_core
        landscape_mesh
        render_core
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

# Sokol is header-only in this project (via vcpkg overlay).
find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

# stb headers (image loading for production preview textures).
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${STB_INCLUDE_DIRS})

# Sokol backend link requirements (sokol_app: D3D11 на Windows, Metal на macOS).
nw_configure_sokol_app(${AppName})
if(MSVC)
    target_link_libraries(${AppName} PRIVATE dwmapi)
endif()

