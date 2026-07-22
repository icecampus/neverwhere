find_package(glm CONFIG REQUIRED)
find_package(FastNoise2 CONFIG REQUIRED)
find_package(CGAL CONFIG QUIET)

nw_add_lib_sources(
    NAME highground_core
    GROUP libs/common
    SOURCES
        pch.h
        include/highground_core/highground.h
        include/highground_core/inspect.h
        include/highground_core/cliff_field.h
        include/highground_core/surface_nets.h
        src/boundary.h
        src/boundary.cpp
        src/cgal_generate.cpp
        src/cliff_field.cpp
        src/contour.cpp
        src/surface_nets.cpp
        src/walls.h
        src/walls.cpp
        src/highground.cpp
    LIBS
        glm::glm-header-only
        topology_core
)

target_include_directories(highground_core PUBLIC include)

# FastNoise2 is used only by the rock-wall builder (src/walls.cpp).
target_link_libraries(highground_core PRIVATE FastNoise2::FastNoise)

# CGAL (boolean region union + constrained Delaunay tops) is desktop-only:
# absent from the Emscripten manifest, so the CGAL generator compiles into an
# inert stub there.
if(CGAL_FOUND)
    target_compile_definitions(highground_core PRIVATE HIGHGROUND_WITH_CGAL=1)
    target_link_libraries(highground_core PRIVATE CGAL::CGAL)
endif()
