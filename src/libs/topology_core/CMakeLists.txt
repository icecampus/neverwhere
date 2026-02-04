find_package(glm CONFIG REQUIRED)

nw_add_lib_sources(
    NAME topology_core
    GROUP libs
    SOURCES
        pch.h
        include/topology_core/staggered_isometry.h
        src/staggered_isometry.cpp
        include/topology_core/camera2d.h
    LIBS
        glm::glm-header-only
)

target_include_directories(topology_core PUBLIC include)

