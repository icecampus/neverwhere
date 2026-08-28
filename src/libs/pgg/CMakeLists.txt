# PGG: procedural geometry graph language — parser/AST/formatter/lint (E0).
# Desktop-only: the wasm runtime ships baked geometry, no parser (spec §13.1).
if(EMSCRIPTEN)
    return()
endif()

find_package(antlr4-runtime CONFIG REQUIRED)

# Generated ANTLR artifacts are committed (regen: tools/pgg/regen_parser.sh).
file(GLOB PARSER_GEN CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/parser_gen/*.cpp")

nw_add_lib_sources(
    NAME pgg
    GROUP libs/common
    SOURCES
        pch.h
        src/ast.h
        src/ast.cpp
        src/compositor.h
        src/compositor.cpp
        src/error_listener.h
        src/error_listener.cpp
        src/formatter.h
        src/formatter.cpp
        src/validate.h
        src/validate.cpp
        src/pgg.cpp
        ${PARSER_GEN}
    LIBS
        antlr4_static
)

target_include_directories(pgg PUBLIC include)

# CI guard against stale generated artifacts (spec §13.1): regenerates the
# parser into a temp dir and diffs against the committed parser_gen/. Exits
# 77 (skip) when no java/jar is available on the machine.
add_test(
    NAME pgg_parser_regen_diff
    COMMAND ${CMAKE_SOURCE_DIR}/tools/pgg/regen_parser.sh --check
)
set_tests_properties(pgg_parser_regen_diff PROPERTIES SKIP_RETURN_CODE 77)
