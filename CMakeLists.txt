cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)
include(cmake/utils.cmake)

project(Neverwhere VERSION 0.1.0 LANGUAGES CXX C)
print_header("Neverwhere")
check_preprocessor_configuration()
#use grouping
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick Widgets Gui)
find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

qt_policy(SET QTP0001 OLD )
qt_policy(SET QTP0004 NEW )
qt_standard_project_setup()

add_subdirectory(src/libs)
add_subdirectory(src/apps)

qt_finalize_project()