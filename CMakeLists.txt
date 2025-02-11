cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)

project(Neverwhere VERSION 0.1.0 LANGUAGES CXX C)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick Widgets)

qt_policy(SET QTP0001 OLD )
qt_policy(SET QTP0004 NEW )
qt_standard_project_setup()

add_subdirectory(src/app)

qt_finalize_project()