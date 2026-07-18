find_package(imgui CONFIG REQUIRED)
find_package(FastNoise2 CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        topology_core
        landscape_core
        FastNoise2::FastNoise
)

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

if(MSVC)
    target_link_libraries(${AppName} PRIVATE d3d11 dxgi)
endif()
