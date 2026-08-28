find_package(GTest REQUIRED)

# Collect every *_test.cpp under src/tests/<domain>/ recursively. New test
# suites land here as src/tests/<domain>/<name>_test.cpp and are picked up
# automatically — no manual source listing needed.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp"
)

add_executable(neverwhere_tests ${TEST_SOURCES})

target_include_directories(neverwhere_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
)

target_link_libraries(neverwhere_tests PRIVATE
    GTest::gtest_main
    core
    math
    base_data
    game_data
    landscape_core
    landscape_mesh
    highground_core
    stone_gen
    fence_core
    render_core
    topology_core
    utils
    Qt6::Core
    Qt6::Quick
)

# pgg is desktop-only; its suites live under src/tests/pgg/.
if(TARGET pgg)
    target_link_libraries(neverwhere_tests PRIVATE pgg)
    target_compile_definitions(neverwhere_tests PRIVATE
        PGG_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgg/corpus"
    )
else()
    file(GLOB PGG_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/pgg/*_test.cpp")
    if(PGG_TEST_SOURCES)
        message(FATAL_ERROR "src/tests/pgg requires the pgg target (desktop builds only)")
    endif()
endif()

if(MSVC)
    target_compile_options(neverwhere_tests PRIVATE "/MP")
endif()

# Group in IDE solution tree (Visual Studio "tests" folder, next to libs/_apps).
set_property(TARGET neverwhere_tests PROPERTY FOLDER tests)

# Test registration (enable_testing() lives in the root CMakeLists.txt)
add_test(NAME neverwhere_tests COMMAND neverwhere_tests)
