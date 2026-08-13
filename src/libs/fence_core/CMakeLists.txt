find_package(glm CONFIG REQUIRED)

nw_add_lib_sources(
    NAME fence_core
    GROUP libs/common
    SOURCES
        pch.h
        include/fence_core/fence_model.h
        include/fence_core/fence_mesh.h
        src/fence_model.cpp
        src/fence_mesh.cpp
    LIBS
        glm::glm-header-only
)

target_include_directories(fence_core PUBLIC include)
