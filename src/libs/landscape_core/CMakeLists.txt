nw_add_lib_sources(
    NAME landscape_core
    GROUP libs/client
    SOURCES
        pch.h
        include/landscape_core/landscape_logic.h
        include/landscape_core/sun_shadow.h
        src/landscape_logic.cpp
        src/sun_shadow.cpp
)

target_include_directories(landscape_core PUBLIC include)
