# PggTool: CLI front-end for the PGG language (check/fmt/ast).
if(EMSCRIPTEN)
    return()
endif()

nw_add_console_app(
    GROUP apps/tools
    LIBS
        pgg
)
