vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Auburn/FastNoise2
    REF v1.1.1
    SHA512 6a6c4cd73a9a03f46749589837d76dba705e78d32748cc438cf875285a7f96c9654ffa8cf10e89ef2ee0b48de23fadb84da9cfff7c11df21794f9406c3817666
    HEAD_REF master
)

vcpkg_from_github(
    OUT_SOURCE_PATH FASTSIMD_SOURCE_PATH
    REPO Auburn/FastSIMD
    REF 16450dae9528727e500e7254f635a671f9c7ee2d
    SHA512 e7d97a4bff643e99c93cb7a01d488bd7f8f187425f3d68e7cdba18d032ca2154c19e61346dd29fb69a94a708f219d5d357b544b7375f2b9c28d9694ebd7d0b4d
    HEAD_REF master
)

vcpkg_replace_string(
    "${SOURCE_PATH}/src/CMakeLists.txt"
[[CPMAddPackage(
    NAME FastSIMD
    GITHUB_REPOSITORY Auburn/FastSIMD
    GIT_TAG 16450dae9528727e500e7254f635a671f9c7ee2d
)]]
    "add_subdirectory(\"${FASTSIMD_SOURCE_PATH}\" \"${CURRENT_BUILDTREES_DIR}/fastsimd-build\")"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DFASTNOISE2_TOOLS=OFF
        -DFASTNOISE2_TESTS=OFF
        -DFASTNOISE2_UTILITY=OFF
        -DBUILD_SHARED_LIBS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/FastNoise2)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
