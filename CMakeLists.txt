cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)

# if (POLICY CMP0141)
#   cmake_policy(SET CMP0141 NEW)
#   set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
# endif()

project(Neverwhere VERSION 0.1.0 LANGUAGES CXX C)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QmlImportScanner)

qt_policy(SET QTP0001 NEW )
qt_policy(SET QTP0004 NEW )
qt_standard_project_setup()

add_subdirectory(src/app)

qt_finalize_project()