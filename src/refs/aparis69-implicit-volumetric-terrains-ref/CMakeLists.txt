find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _refs
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        glm::glm-header-only
        render_core
)

target_include_directories(${AppName} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ivt/Include"
)

find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

if(MSVC)
    target_compile_definitions(${AppName} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${AppName} PRIVATE d3d11 dxgi)
endif()

message(STATUS "aparis69-implicit-volumetric-terrains-ref: IVT viewer + batch OBJ exporter (Paris 2019 TOG)")
