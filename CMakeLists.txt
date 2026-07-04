cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)
include(cmake/utils.cmake)

project(Neverwhere VERSION 0.1.0 LANGUAGES CXX C)
print_header("Neverwhere")
check_preprocessor_configuration()
configure_common_param()
if(MSVC)
    add_compile_options(/MP)
endif()

#use grouping
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS container_hash uuid lexical_cast)

if(EMSCRIPTEN AND NOT TARGET WrapRt::WrapRt)
    add_library(WrapRt::WrapRt INTERFACE IMPORTED)
    message(STATUS "Создан фиктивный таргет WrapRt::WrapRt для Emscripten")
    
    # Отключаем проверку RT в Qt
    set(QT_FEATURE_rt OFF CACHE BOOL "Disable rt feature" FORCE)
    set(QT_SKIP_AUTO_WRAPPER WrapRt CACHE BOOL "Skip WrapRt wrapper" FORCE)
endif()

if(APPLE)
    set(OpenGL_GL_PREFERENCE "GLVND" CACHE STRING "" FORCE)
    set(OPENGL_INCLUDE_DIR "/System/Library/Frameworks/OpenGL.framework/Headers" CACHE PATH "" FORCE)
    set(OPENGL_gl_LIBRARY "/System/Library/Frameworks/OpenGL.framework/OpenGL" CACHE FILEPATH "" FORCE)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core )
find_package(Qt6 REQUIRED COMPONENTS Qml Quick Widgets Gui QuickControls2 ShaderTools)
find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-noise CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(rttr CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
  find_package(spdlog CONFIG REQUIRED)
  find_package(unofficial-noise CONFIG REQUIRED)
  qt_policy(SET QTP0001 OLD )
  qt_policy(SET QTP0004 NEW )
endif()
qt_standard_project_setup()


if(EMSCRIPTEN)
  add_compile_options(-sDISABLE_EXCEPTION_CATCHING=1)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -sUSE_SDL=2 ${PRELOAD_FILES}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -sUSE_SDL=2 ${PRELOAD_FILES}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sWASM=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sNO_DISABLE_EXCEPTION_CATCHING") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sALLOW_MEMORY_GROWTH=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sFORCE_FILESYSTEM=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sNO_EXIT_RUNTIME=0") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sASSERTIONS=1") 
  # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sNO_FILESYSTEM=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_FUNCTIONS=_main") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_RUNTIME_METHODS=ccall,cwrap") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lembind") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sFETCH -sFILESYSTEM=1")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sFULL_ES2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2")
endif()

add_subdirectory(src/libs)
add_subdirectory(src/apps)
add_subdirectory(src/tests)
add_subdirectory(src/refs)

qt_finalize_project()

