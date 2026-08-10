# Обёртка над ShapeML (GPL3v3, submodule external/shapeml).
#
# ВАЖНО: ShapeML под GPL3v3 — статическая линковка делает производный бинарь
# GPL при распространении. shapeml_ref линкуется ТОЛЬКО в ShapemlPlayground
# (внутренний инструмент исследования); в EpicMapEditor / EpicGameClient /
# общие libs линковать запрещено.
#
# Их собственная билд-система не используется: корневой CMakeLists ShapeML
# несовместим с CMake 4.x (cmake_minimum_required 3.0), а viewer (OpenGL 4.5 +
# glfw + их imgui) нам не нужен — собираем только core + surface_mesh.
#
# Особенности сборки чужого кода:
# - C++17 (upstream писал под C++14; Eigen 3.3.7 не дружит с C++20).
# - Через -include cstdint / /FI cstdint: vector_types.h полагается на
#   транзитивный <cstdint>, которого больше нет в GCC 13+.
# - Warnings подавлены (-w): чужой код, чинить их мы не будем.

set(SHAPEML_ROOT "${CMAKE_SOURCE_DIR}/external/shapeml")

file(GLOB_RECURSE SHAPEML_SOURCES CONFIGURE_DEPENDS
    "${SHAPEML_ROOT}/src/shapeml/*.cc")
list(FILTER SHAPEML_SOURCES EXCLUDE REGEX "_test\\.cc$")

file(GLOB SURFACE_MESH_SOURCES CONFIGURE_DEPENDS
    "${SHAPEML_ROOT}/external/surface_mesh/src/surface_mesh/*.cpp")

add_library(shapeml_ref STATIC ${SHAPEML_SOURCES} ${SURFACE_MESH_SOURCES})

set_target_properties(shapeml_ref PROPERTIES
    CXX_STANDARD 17
    AUTOMOC OFF
    AUTORCC OFF
    FOLDER _refs
)

# surface_mesh намеренно включает shapeml-заголовки (vector_types.h), поэтому
# все три include-пути общие (как в их CMakeLists).
target_include_directories(shapeml_ref SYSTEM PUBLIC
    "${SHAPEML_ROOT}/src"
    "${SHAPEML_ROOT}/external/eigen"
    "${SHAPEML_ROOT}/external/surface_mesh/src"
)

if(MSVC)
    target_compile_options(shapeml_ref PRIVATE /w /FIcstdint
        /D_USE_MATH_DEFINES /D_ENABLE_EXTENDED_ALIGNED_STORAGE)
else()
    target_compile_options(shapeml_ref PRIVATE -w -include cstdint
        -Wno-deprecated-copy)
endif()
