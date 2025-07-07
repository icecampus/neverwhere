cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)
include(cmake/utils.cmake)

project(Neverwhere VERSION 0.1.0 LANGUAGES CXX C)
print_header("Neverwhere")
check_preprocessor_configuration()
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

find_package(Boost REQUIRED COMPONENTS uuid lexical_cast)

set(CMAKE_CACHE_ARGS
    -QT_FEATURE_rt=OFF  # Отключаем поиск librt
    -QT_SKIP_AUTO_WRAPPER=WrapRt  # Пропускаем обёртку для rt
)
find_package(Qt6 REQUIRED COMPONENTS Core )
# find_package(Qt6 REQUIRED COMPONENTS Qml Quick Widgets Gui QuickControls2 ShaderTools)
# find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
find_package(magic_enum CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

qt_policy(SET QTP0001 OLD )
qt_policy(SET QTP0004 NEW )
qt_standard_project_setup()



if(EMSCRIPTEN)
  add_compile_options(-sDISABLE_EXCEPTION_CATCHING=1)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sWASM=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sALLOW_MEMORY_GROWTH=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sNO_EXIT_RUNTIME=0") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sASSERTIONS=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sDISABLE_EXCEPTION_CATCHING=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sNO_FILESYSTEM=1") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_FUNCTIONS=_main") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_RUNTIME_METHODS=ccall,cwrap") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lembind") 
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ")
endif()

add_subdirectory(src/libs)
add_subdirectory(src/apps)

qt_finalize_project()