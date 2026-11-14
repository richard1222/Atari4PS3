/* pthread_* implementation over LV2 syscalls for the DOSBox Pure
 * runner ONLY. See pthread.h in this directory for scope. Linked into
 * the dosbox-pure runner, never into other runners or the launcher.
 *
 * Conventions: 0 on success, errno-style nonzero on failure (matching
 * POSIX pthread_* return semantics, which return the error number
 * instead of setting errno). */

#include "pthread.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/thread.h> // establishes LV2_SYSCALL before cond.h
#include <sys/mutex.h>
#include <sys/cond.h>

namespace {

const s32 kPriority = 1000; /* usual PPU default, same as the gthreads shim */
const u64 kDefaultStack = 256 * 1024;

/* Trampoline: LV2 thread entries take (void*) and return void; POSIX
 * entries return void*. The return value is kept for pthread_join. */
struct ThreadStart {
  void* (*fn)(void*);
  void* arg;
};

void thread_trampoline(void* p) {
  ThreadStart* st = static_cast<ThreadStart*>(p);
  auto fn=st->fn;
  void* arg=st->arg;
  free(st); // both detached and joinable threads release their trampoline
  void* ret=fn(arg);
  sysThreadExit(static_cast<u64>(reinterpret_cast<uintptr_t>(ret)));
}

int ensure_mutex(pthread_mutex_t* m) {
  if (!m) return EINVAL;
  if (m->handle != 0) return 0;
  sys_mutex_attr_t attr;
  sysMutexAttrInitialize(attr);
  sys_mutex_t created = 0;
  if (sysMutexCreate(&created, &attr) != 0) return EAGAIN;
  m->handle = static_cast<uint64_t>(created);
  m->init = 1;
  return 0;
}

} /* namespace */

extern "C" {

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg) {
  if (!thread || !start_routine) return EINVAL;
  ThreadStart* st =
      static_cast<ThreadStart*>(malloc(sizeof(ThreadStart)));
  if (!st) return EAGAIN;
  st->fn = start_routine;
  st->arg = arg;

  u64 stacksize = kDefaultStack;
  u64 flags = THREAD_JOINABLE;
  if (attr) {
    if (attr->stacksize > 0) stacksize = attr->stacksize;
    if (attr->detachstate == PTHREAD_CREATE_DETACHED) flags = 0;
  }
  if (stacksize < 64 * 1024) stacksize = 64 * 1024;

  static char name[] = "pthread";
  sys_ppu_thread_t id = 0;
  s32 ret = sysThreadCreate(&id, thread_trampoline, st, kPriority,
                            stacksize, flags, name);
  if (ret != 0) {
    free(st);
    return EAGAIN;
  }
  *thread = static_cast<pthread_t>(id);
  return 0;
}

int pthread_detach(pthread_t thread) {
  return sysThreadDetach(static_cast<sys_ppu_thread_t>(thread)) == 0 ? 0
                                                                     : EINVAL;
}

int pthread_join(pthread_t thread, void** retval) {
  // The trampoline passes the actual start-routine result to sysThreadExit.
  u64 status = 0;
  s32 ret = sysThreadJoin(static_cast<sys_ppu_thread_t>(thread), &status);
  if (ret != 0) return EINVAL;
  if (retval) *retval = reinterpret_cast<void*>(status);
  return 0;
}

pthread_t pthread_self(void) {
  /* LV2 exposes no current-thread-id query through PSL1GHT. Pure
   * never calls pthread_self; return a fixed nonzero id so any
   * accidental use degrades to "same thread" rather than crashing. */
  return (pthread_t)1;
}

int pthread_equal(pthread_t t1, pthread_t t2) {
  return t1 == t2 ? 1 : 0;
}

int pthread_attr_init(pthread_attr_t* attr) {
  if (!attr) return EINVAL;
  attr->stacksize = 0;
  attr->detachstate = PTHREAD_CREATE_JOINABLE;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
  if (!attr) return EINVAL;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize) {
  if (!attr) return EINVAL;
  attr->stacksize = stacksize;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* attr,
                              size_t* stacksize) {
  if (!attr || !stacksize) return EINVAL;
  *stacksize = attr->stacksize ? attr->stacksize : (size_t)kDefaultStack;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate) {
  if (!attr) return EINVAL;
  if (detachstate != PTHREAD_CREATE_JOINABLE &&
      detachstate != PTHREAD_CREATE_DETACHED)
    return EINVAL;
  attr->detachstate = detachstate;
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr,
                                int* detachstate) {
  if (!attr || !detachstate) return EINVAL;
  *detachstate = attr->detachstate;
  return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex,
                       const pthread_mutexattr_t* attr) {
  (void)attr; /* recursive/normal distinction unused by Pure */
  if (!mutex) return EINVAL;
  mutex->handle = 0;
  mutex->init = 0;
  return ensure_mutex(mutex);
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
  if (!mutex) return EINVAL;
  if (mutex->handle != 0)
    sysMutexDestroy(static_cast<sys_mutex_t>(mutex->handle));
  mutex->handle = 0;
  mutex->init = 0;
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
  int rc = ensure_mutex(mutex);
  if (rc != 0) return rc;
  return sysMutexLock(static_cast<sys_mutex_t>(mutex->handle), 0) == 0
             ? 0
             : EINVAL;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
  int rc = ensure_mutex(mutex);
  if (rc != 0) return rc;
  return sysMutexTryLock(static_cast<sys_mutex_t>(mutex->handle)) == 0
             ? 0
             : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
  if (!mutex || mutex->handle == 0) return EINVAL;
  return sysMutexUnlock(static_cast<sys_mutex_t>(mutex->handle)) == 0
             ? 0
             : EINVAL;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
  (void)attr;
  if (!cond) return EINVAL;
  cond->handle = 0;
  cond->bound_mutex = 0;
  cond->init = 1;
  return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) {
  if (!cond) return EINVAL;
  if (cond->handle != 0)
    sysCondDestroy(static_cast<sys_cond_t>(cond->handle));
  cond->handle = 0;
  cond->bound_mutex = 0;
  cond->init = 0;
  return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
  if (!cond || !mutex) return EINVAL;
  int rc = ensure_mutex(mutex);
  if (rc != 0) return rc;
  uint64_t mh = mutex->handle;
  if (cond->handle == 0 || cond->bound_mutex != mh) {
    /* (Re)bind the sysCond to this mutex. */
    if (cond->handle != 0)
      sysCondDestroy(static_cast<sys_cond_t>(cond->handle));
    sys_cond_attr_t attr;
    sysCondAttrInitialize(attr);
    sys_cond_t created = 0;
    if (sysCondCreate(&created, static_cast<sys_mutex_t>(mh), &attr) != 0) {
      cond->handle = 0;
      cond->bound_mutex = 0;
      return EAGAIN;
    }
    cond->handle = static_cast<uint64_t>(created);
    cond->bound_mutex = mh;
    cond->init = 1;
  }
  /* sysCondWait atomically releases the bound mutex, waits, and
   * re-acquires it; timeout 0 = wait forever. */
  return sysCondWait(static_cast<sys_cond_t>(cond->handle), 0) == 0 ? 0
                                                                   : EINVAL;
}

int pthread_cond_signal(pthread_cond_t* cond) {
  if (!cond) return EINVAL;
  if (cond->handle == 0) return 0; /* nobody waiting: POSIX no-op */
  return sysCondSignal(static_cast<sys_cond_t>(cond->handle)) == 0 ? 0
                                                                   : EINVAL;
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
  if (!cond) return EINVAL;
  if (cond->handle == 0) return 0;
  return sysCondBroadcast(static_cast<sys_cond_t>(cond->handle)) == 0
             ? 0
             : EINVAL;
}

} /* extern "C" */
