nw_add_lib_sources(
    NAME landscape_mesh
    GROUP libs
    SOURCES
        pch.h
        include/landscape_mesh/landscape_mesh.h
        src/landscape_mesh.cpp
    LIBS
        landscape_core
)

target_include_directories(landscape_mesh PUBLIC include)
