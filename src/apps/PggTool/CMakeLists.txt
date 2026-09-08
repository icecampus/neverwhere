# PggTool: CLI front-end for the PGG language (check/fmt/ast).
if(EMSCRIPTEN)
    return()
endif()

find_package(nlohmann_json CONFIG REQUIRED)

nw_add_console_app(
    GROUP apps/tools
    LIBS
        pgg
        nlohmann_json::nlohmann_json
)
