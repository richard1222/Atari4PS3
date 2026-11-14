// Internal helpers for the Atari4PS3 PSL1GHT threading shim.
// NOT a standard header: included only by the shim's shadow headers
// (mutex, thread, condition_variable) in this same directory.
//
// Why this exists: the ps3toolchain libstdc++ was built without gthreads,
// so <mutex>, <thread> and <condition_variable> declare nothing usable and
// the C99 stdio functions are missing from namespace std. The shim
// re-implements the subset Stella uses on top of the LV2 syscalls
// (sysMutex / sysCond / sysThread) that PSL1GHT exposes.

#ifndef A4_PS3_DETAIL_H
#define A4_PS3_DETAIL_H

#include <atomic>
#include <cstdint>

#include <sys/mutex.h>
#include <sys/cond.h>
#include <sys/thread.h>

namespace a4_shim {

// --- Spin lock for lazy one-time creation guards --------------------------
// Used only to serialize the first creation of a kernel object; never held
// across a blocking call.
class SpinLock {
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

 public:
  SpinLock() = default;
  SpinLock(const SpinLock&) = delete;
  SpinLock& operator=(const SpinLock&) = delete;
  void lock() noexcept {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      // Spin briefly; creation is rare and fast.
    }
  }
  void unlock() noexcept { flag_.clear(std::memory_order_release); }
};

class SpinGuard {
  SpinLock& lock_;

 public:
  explicit SpinGuard(SpinLock& lock) : lock_(lock) { lock_.lock(); }
  ~SpinGuard() { lock_.unlock(); }
  SpinGuard(const SpinGuard&) = delete;
  SpinGuard& operator=(const SpinGuard&) = delete;
};

// --- Thread identity registry --------------------------------------------
// LV2 exposes no "current thread id" query through PSL1GHT's headers, so
// identity is tracked by stack address: every std::thread registers the
// stack address reported by sysThreadGetStackInformation() when it starts.
// The main thread and threads created outside std::thread (e.g. SDL's audio
// thread) are never registered and share the reserved id 1. No Stella code
// needs to distinguish those threads from each other; the only comparisons
// in the codebase are "is this the worker thread" checks, which work because
// worker threads always have distinct ids >= 2.
inline constexpr std::uint64_t kNoThreadId = 0;
inline constexpr std::uint64_t kMainThreadId = 1;

class ThreadRegistry {
  static constexpr int kMax = 64;
  struct Entry {
    std::uint64_t stackAddr{0};
    std::uint64_t seq{0};
  };
  Entry entries_[kMax];
  int count_{0};
  SpinLock lock_;
  std::atomic<std::uint64_t> nextSeq_;

 public:
  ThreadRegistry() : nextSeq_(kMainThreadId) {}
  static ThreadRegistry& instance() {
    static ThreadRegistry reg;
    return reg;
  }

  // First std::thread gets id 2; 0 means "no thread", 1 means
  // "main thread or thread created outside std::thread".
  std::uint64_t allocSeq() { return ++nextSeq_; }

  void registerCurrent(std::uint64_t seq) {
    sys_ppu_thread_stack_t info;
    if (sysThreadGetStackInformation(&info) != 0) return;
    const std::uint64_t addr = reinterpret_cast<std::uint64_t>(info.addr);
    SpinGuard guard(lock_);
    for (int i = 0; i < count_; ++i) {
      if (entries_[i].stackAddr == addr) {
        entries_[i].seq = seq;  // stack address reused by a new thread
        return;
      }
    }
    if (count_ < kMax) {
      entries_[count_].stackAddr = addr;
      entries_[count_].seq = seq;
      ++count_;
    }
    // If the table is full the thread still runs; get_id() just reports the
    // main id for it. Stella never has more than a couple of std::threads.
  }

  std::uint64_t currentSeq() {
    sys_ppu_thread_stack_t info;
    if (sysThreadGetStackInformation(&info) != 0) return kMainThreadId;
    const std::uint64_t addr = reinterpret_cast<std::uint64_t>(info.addr);
    SpinGuard guard(lock_);
    for (int i = 0; i < count_; ++i) {
      if (entries_[i].stackAddr == addr) return entries_[i].seq;
    }
    return kMainThreadId;
  }
};

}  // namespace a4_shim

#endif  // A4_PS3_DETAIL_H
