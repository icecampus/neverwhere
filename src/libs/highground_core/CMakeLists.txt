find_package(glm CONFIG REQUIRED)
find_package(FastNoise2 CONFIG REQUIRED)

nw_add_lib_sources(
    NAME highground_core
    GROUP libs/common
    SOURCES
        pch.h
        include/highground_core/highground.h
        include/highground_core/inspect.h
        src/contour.cpp
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
