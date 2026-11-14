/* Shadow <pthread.h> for the DOSBox Pure core build ONLY (M2).
 *
 * DOSBox Pure's threading layer (include/dbp_threads.h) uses real
 * pthread_* calls (create/mutex/cond). The ps3toolchain sysroot
 * pthread.h is a stub: the declarations exist but newlib provides no
 * working implementation behind them, so threads created through it
 * would silently never run. This header + pthread_ps3.cxx implement
 * the subset Pure uses on top of the LV2 syscalls (sysThread /
 * sysMutex / sysCond), following the same patterns as the gthreads
 * shim in shim/std/.
 *
 * Scope: this directory is added to the include path ONLY for the
 * DOSBox Pure core build (-I shim/pthread) and pthread_ps3.cxx is
 * linked ONLY into the dosbox-pure runner. No other core or binary
 * sees it. Deliberately NOT provided: pthread_exit, timed waits,
 * barriers, rwlocks, spinlocks, keys, cancellation -- Pure uses none
 * of them (verified by grep, 2026-11-14). If a future core needs more,
 * extend here rather than touching the sysroot.
 */
#ifndef A4_PS3_PTHREAD_H
#define A4_PS3_PTHREAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread id: the LV2 PPU thread id. */
typedef uint64_t pthread_t;

typedef struct {
  size_t stacksize; /* 0 = default */
  int detachstate;  /* PTHREAD_CREATE_JOINABLE / PTHREAD_CREATE_DETACHED */
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

/* Mutex: kernel handle created lazily, so statically-initialized
 * mutexes (PTHREAD_MUTEX_INITIALIZER) also work. */
typedef struct {
  uint64_t handle;
  int init;
} pthread_mutex_t;

typedef struct {
  int unused;
} pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0, 0 }

/* Condition variable: an LV2 sysCond is bound to ONE mutex at
 * creation, so the handle is created lazily on the first wait and
 * rebound (destroy + recreate) if a different mutex ever arrives --
 * the same approach as the shim <condition_variable>. */
typedef struct {
  uint64_t handle;
  uint64_t bound_mutex;
  int init;
} pthread_cond_t;

typedef struct {
  int unused;
} pthread_condattr_t;

#define PTHREAD_COND_INITIALIZER { 0, 0, 0 }

/* Threads */
int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);
int pthread_detach(pthread_t thread);
int pthread_join(pthread_t thread, void** retval);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* stacksize);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate);

/* Mutexes */
int pthread_mutex_init(pthread_mutex_t* mutex,
                       const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

/* Condition variables */
int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

#ifdef __cplusplus
}
#endif

#endif
