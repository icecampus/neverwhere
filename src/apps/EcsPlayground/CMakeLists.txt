find_package(glad CONFIG REQUIRED)

# Sokol is header-only in this project (via vcpkg).
find_path(SOKOL_INCLUDE_DIRS "sokol_gfx.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_gfx.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()

nw_add_qml_app(
    GROUP
        _apps
    LIBS
        EnTT::EnTT
        spdlog::spdlog_header_only
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        glad::glad
)

target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

# GL glue (graphics/sokol_impl.cpp) renders inside a Qt OpenGL context.
if(EMSCRIPTEN)
    # nothing extra
elseif(MSVC)
    target_link_libraries(${AppName} PRIVATE d3d11 dxgi)
elseif(APPLE)
    target_link_libraries(${AppName} PRIVATE "-framework Metal" "-framework Cocoa" "-framework QuartzCore")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(${AppName} PRIVATE OpenGL::GL)
endif()
