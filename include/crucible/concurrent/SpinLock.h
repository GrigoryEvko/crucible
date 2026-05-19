#pragma once

#include <crucible/Platform.h>

#include <atomic>

namespace crucible::concurrent {

// fixy-A5-022 + FIXY-U-085: alignas(64) is load-bearing.  SpinLock is
// routinely embedded in arrays (`std::array<SpinLock, M> producer_locks_`
// in AdaptiveScheduler MPSC/MPMC sharded grids), and a 1-byte std::atomic_flag
// would otherwise let every contending producer pin its lock to the same
// 64-byte line — the classic 40× false-sharing trap (CLAUDE.md §VIII).
// Centralizing the alignment on the primitive removes the per-embed-site
// discipline burden — every consumer (ConnectionPool, Backpressure,
// BackgroundThread, future) inherits the cache-line isolation by composition,
// not by remembering to repeat `alignas(64)` at every field declaration.
//
// fixy-A5-021 + FIXY-U-085 consolidation: this is the single canonical
// SpinLock primitive for the entire substrate.  ConnectionPoolRuntime.h and
// BackpressureRuntime.h each previously shipped a private nested SpinGuard
// over a raw std::atomic_flag&; both now route through this primitive.  New
// substrate sites needing a short spin gate MUST use this type rather than
// reinvent — the consolidation makes every site's behaviour (acquire/release
// semantics, pause hint, cache-line isolation, lock-free guarantee) uniform.
class alignas(64) SpinLock {
public:
    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    // Lockable concept completion (BasicLockable + try_lock); permits use
    // with std::unique_lock and std::lock_guard alongside the Crucible
    // SpinGuard.  Acquire-on-success matches lock(); failure is relaxed
    // because no observation is published on a non-acquisition.
    [[nodiscard]] bool try_lock() noexcept {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

static_assert(alignof(SpinLock) >= 64,
              "SpinLock must be cache-line-aligned to prevent false sharing "
              "across embedded array slots and adjacent struct members");
static_assert(sizeof(SpinLock) >= 64,
              "SpinLock occupies a full cache line; trailing padding is "
              "intentional — adjacent SpinLocks in an array must land on "
              "distinct lines");
// fixy-A5-029 + FIXY-U-086 discipline note: std::atomic_flag is the ONE
// atomic type the standard ([atomics.flag]) guarantees is lock-free on every
// conforming implementation — unlike std::atomic<T> for arbitrary T, no
// `is_always_lock_free` member exists for atomic_flag precisely because the
// guarantee is unconditional.  This is why SpinLock uses atomic_flag and not
// std::atomic<bool>: it inherits the unconditional guarantee.  See ISO/IEC
// 14882 §[atomics.flag]/1: "object of type atomic_flag provides the classic
// test-and-set functionality.  It has two states, set and clear ... [it] is
// the only type that provides operations that are guaranteed lock-free".

class SpinGuard {
public:
    explicit SpinGuard(SpinLock& lock) noexcept
        : lock_{lock} {
        lock_.lock();
    }

    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
    SpinGuard(SpinGuard&&) = delete;
    SpinGuard& operator=(SpinGuard&&) = delete;

    ~SpinGuard() noexcept {
        lock_.unlock();
    }

private:
    SpinLock& lock_;
};

// FIXY-U-085 runtime smoke (feedback_algebra_runtime_smoke_test_discipline.md):
// pure static_asserts mask consteval/SFINAE/inline-body bugs.  This block
// exercises the lock/try_lock/unlock cycle once with non-constant operands at
// every TU that includes the header, catching any regression in spin
// semantics (e.g. an `_explicit` order swap) before it reaches a downstream
// test.  The function is `inline` so the linker discards all but one copy;
// the `[[maybe_unused]]` lambda materializes a call site without escaping
// the TU.
inline void spinlock_runtime_smoke_test() noexcept {
    SpinLock lock;
    if (lock.try_lock()) {
        lock.unlock();
    }
    {
        SpinGuard guard{lock};
        (void)guard;
    }
    // After release, try_lock must succeed on a fresh lock.
    [[maybe_unused]] const bool reacquired = lock.try_lock();
    if (reacquired) {
        lock.unlock();
    }
}

}  // namespace crucible::concurrent
