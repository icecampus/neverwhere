# Copy patches from the original port
if(EXISTS "${VCPKG_ROOT_DIR}/ports/qtbase")
    file(GLOB _qtbase_patches
        "${VCPKG_ROOT_DIR}/ports/qtbase/*.patch"
        "${VCPKG_ROOT_DIR}/ports/qtbase/*.diff")
    if(_qtbase_patches)
        file(COPY ${_qtbase_patches} DESTINATION "${CURRENT_PORT_DIR}")
    endif()
endif()

# Include the base portfile
include("${VCPKG_ROOT_DIR}/ports/qtbase/portfile.cmake")

if(VCPKG_TARGET_IS_EMSCRIPTEN)
    # Remove pthread injection for Emscripten from installed Qt files
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "SHELL:-pthread" "")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" " -pthread" " ")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Targets.cmake" "-pthread" "")
    
    # Also neutralize additions in helpers/dependencies which toggle pthreads
    if(EXISTS "${CURRENT_PACKAGES_DIR}/share/Qt6/QtWasmHelpers.cmake")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/QtWasmHelpers.cmake" "SHELL:-pthread" "")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/QtWasmHelpers.cmake" "-pthread" "")
    endif()
    
    if(EXISTS "${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Dependencies.cmake")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Dependencies.cmake" "set(THREADS_PREFER_PTHREAD_FLAG TRUE)" "# removed by overlay")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6/Qt6Dependencies.cmake" "find_package(Threads)" "# find_package(Threads) disabled by overlay")
    endif()
    
    # Remove pthread-related symbols from Qt6CoreTargets
    if(EXISTS "${CURRENT_PACKAGES_DIR}/share/Qt6Core/Qt6CoreTargets.cmake")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6Core/Qt6CoreTargets.cmake" "SHELL:-pthread" "")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6Core/Qt6CoreTargets.cmake" "-pthread" "")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Qt6Core/Qt6CoreTargets.cmake" "Threads::Threads" "")
    endif()
    
    # Stub out pthread functions that Qt tries to use
    file(WRITE "${CURRENT_PACKAGES_DIR}/include/Qt6/QtCore/qthread_emscripten_stubs.h" "
#ifndef QTHREAD_EMSCRIPTEN_STUBS_H
#define QTHREAD_EMSCRIPTEN_STUBS_H

#ifdef __EMSCRIPTEN__
#ifdef __cplusplus
extern \"C\" {
#endif

// Stub implementations for pthread scheduling functions
inline int sched_get_priority_min(int policy) { return 0; }
inline int sched_get_priority_max(int policy) { return 0; }
inline int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param) { return 0; }

// Stub for sem_timedwait
struct timespec;
inline int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout) {
    // Simple busy-wait implementation
    while (sem_trywait(sem) != 0) {
        // Could add timeout check here
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif // __EMSCRIPTEN__

#endif // QTHREAD_EMSCRIPTEN_STUBS_H
")
endif()