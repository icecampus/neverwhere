find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

nw_add_console_app(
    GROUP
        _apps
    LIBS
        spdlog::spdlog_header_only
        imgui::imgui
        glm::glm-header-only
        render_core
)

# Sokol is header-only in this project (via vcpkg overlay).
find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

# stb headers (image loading for warping texture).
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${STB_INCLUDE_DIRS})

# Optional OpenMP for the Rock Fracture scenario (BFS / convolution).
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${AppName} PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(${AppName} PRIVATE ROCK_FRACTURE_USE_OPENMP)
    message(STATUS "RenderPlayground: OpenMP enabled for Rock Fracture")
else()
    message(STATUS "RenderPlayground: OpenMP not found, Rock Fracture will run single-threaded")
endif()

# Copy resources (textures used by warping field) to the build directory.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    add_custom_command(TARGET ${AppName} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${AppName}>/resources"
        VERBATIM
    )
endif()

# Sokol backend link requirements (standalone app uses sokol_app + D3D11 on Windows).
if(MSVC)
    target_link_libraries(${AppName} PRIVATE d3d11 dxgi)
endif()
