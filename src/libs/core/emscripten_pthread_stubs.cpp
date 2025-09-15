/**
 * Stub implementations for pthread and scheduler functions
 * that Qt6Core expects but are not available in Emscripten without -pthread
 */

#ifdef __EMSCRIPTEN__

#include <cstddef>
#include <cerrno>

extern "C" {

// Scheduler priority functions
int sched_get_priority_min(int policy) {
    // Return a reasonable minimum priority
    return 1;
}

int sched_get_priority_max(int policy) {
    // Return a reasonable maximum priority  
    return 99;
}

// pthread scheduling
// struct sched_param {
//     int sched_priority;
// };

typedef unsigned long pthread_t;

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param) {
    // No-op for Emscripten without pthreads
    return 0;
}

// Semaphore functions
typedef struct {
    int value;
} sem_t;

// struct timespec {
//     long tv_sec;
//     long tv_nsec;
// };

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout) {
    // Simple stub - in real Emscripten without pthreads,
    // Qt shouldn't be using threaded code paths anyway
    return -1; // Return error to force Qt to use non-blocking paths
}

// int sem_trywait(sem_t *sem) {
//     // Non-blocking semaphore wait
//     if (sem && sem->value > 0) {
//         sem->value--;
//         return 0;
//     }
//     errno = EAGAIN;
//     return -1;
// }

} // extern "C"

#endif // __EMSCRIPTEN__
