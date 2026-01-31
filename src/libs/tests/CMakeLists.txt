find_package(GTest REQUIRED)

add_executable(landscape_tests 
    landscape_test.cpp
)

target_include_directories(landscape_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
)

target_link_libraries(landscape_tests PRIVATE
    GTest::gtest_main
    core
    math
    base_data
    generators
    utils
    Qt6::Core
    Qt6::Quick
)

if(MSVC)
    target_compile_options(landscape_tests PRIVATE "/MP")
endif()

# Enable testing
enable_testing()
add_test(NAME landscape_tests COMMAND landscape_tests)
