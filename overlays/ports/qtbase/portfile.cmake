include("${VCPKG_ROOT_DIR}/ports/qtbase/portfile.cmake")

if(VCPKG_TARGET_IS_EMSCRIPTEN)
    # Remove pthread injection for Emscripten from installed Qt files
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "SHELL:-pthread" "")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" " -pthread" " ")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "Threads::Threads" "")

    # Also neutralize additions in helpers/dependencies which toggle pthreads
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/QtWasmHelpers.cmake" "SHELL:-pthread" "")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Dependencies.cmake" "set(THREADS_PREFER_PTHREAD_FLAG TRUE)" "# removed by overlay")
endif()


