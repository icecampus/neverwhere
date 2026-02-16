if(EMSCRIPTEN)
    set(GRAPHICS_LIBS)
elseif(MSVC)
    set(GRAPHICS_LIBS d3d11 dxgi)
elseif(APPLE)
    set(GRAPHICS_LIBS "-framework Metal" "-framework Cocoa" "-framework QuartzCore")
else()
    find_package(OpenGL REQUIRED)
    set(GRAPHICS_LIBS OpenGL::GL)
endif()

find_package(glad CONFIG REQUIRED)
find_path(SOKOL_INCLUDE_DIRS "sokol_gfx.h")

nw_add_lib_sources(
    NAME graphics
    GROUP libs
    SOURCES
        lib.h
        sokol_impl.cpp
    LIBS
        Qt6::Core
        Qt6::Quick
        spdlog::spdlog_header_only
        glad::glad
        ${GRAPHICS_LIBS}
)

target_include_directories(graphics PUBLIC ${SOKOL_INCLUDE_DIRS})
