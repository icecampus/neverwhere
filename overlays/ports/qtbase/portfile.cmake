if(EXISTS "${VCPKG_ROOT_DIR}/ports/qtbase")
    file(GLOB _qtbase_patches
        "${VCPKG_ROOT_DIR}/ports/qtbase/*.patch"
        "${VCPKG_ROOT_DIR}/ports/qtbase/*.diff")
    if(_qtbase_patches)
        file(COPY ${_qtbase_patches} DESTINATION "${CURRENT_PORT_DIR}")
    endif()
endif()

include("${VCPKG_ROOT_DIR}/ports/qtbase/portfile.cmake")

if(VCPKG_TARGET_IS_EMSCRIPTEN)
    # Remove pthread injection for Emscripten from installed Qt files
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "SHELL:-pthread" "")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" " -pthread" " ")
    # vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "Threads::Threads" "")

    # Also neutralize additions in helpers/dependencies which toggle pthreads
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/QtWasmHelpers.cmake" "SHELL:-pthread" "")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Dependencies.cmake" "set(THREADS_PREFER_PTHREAD_FLAG TRUE)" "# removed by overlay")
endif()


-feature-thread -no-feature-sharedmemory -no-feature-systemsemaphore

