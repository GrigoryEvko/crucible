#pragma once

#include <crucible/Platform.h>

#include <atomic>
#include <cstddef>
#include <thread>

namespace crucible::concurrent {

// The alignment lives on the primitive rather than at each embedding field, so
// a lock placed in an array or next to another lock is isolated without the
// embedding site having to remember it.
class alignas(64) SpinLock {
public:
    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    void lock() noexcept {
        // A pause-only spin burns the waiter's core for the holder's whole
        // scheduling quantum whenever the holder is descheduled between
        // acquire and release. The bounded pause phase covers the common case,
        // where the holder releases almost at once, and the escalation to
        // yield lets the scheduler run a holder that has lost its core. That
        // caps the wasted CPU at the pause budget.
        constexpr std::size_t kPauseBeforeYield = 64;
        std::size_t spin_iters = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            if (spin_iters < kPauseBeforeYield) {
                CRUCIBLE_SPIN_PAUSE;
                ++spin_iters;
            } else {
                std::this_thread::yield();
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

static_assert(alignof(SpinLock) >= 64, "SpinLock must be cache-line-aligned to prevent false sharing "
                                       "across embedded array slots and adjacent struct members");
static_assert(sizeof(SpinLock) >= 64, "SpinLock occupies a full cache line; trailing padding is "
                                      "intentional — adjacent SpinLocks in an array must land on "
                                      "distinct lines");
// The storage is std::atomic_flag rather than std::atomic<bool> because
// [atomics.flag] makes it the only type whose operations are guaranteed
// lock-free on every conforming implementation. The guarantee is
// unconditional, which is why the type carries no is_always_lock_free member
// to test.

class SpinGuard {
public:
    explicit SpinGuard(SpinLock& lock) noexcept : lock_{lock} { lock_.lock(); }

    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
    SpinGuard(SpinGuard&&) = delete;
    SpinGuard& operator=(SpinGuard&&) = delete;

    ~SpinGuard() noexcept { lock_.unlock(); }

private:
    SpinLock& lock_;
};

// This exercises the acquire and release paths with non-constant operands. A
// check built only from static_assert never instantiates those bodies, so a
// broken memory order in one of them would go unnoticed.
inline void spinlock_runtime_smoke_test() noexcept {
    SpinLock lock;
    if (lock.try_lock()) {
        lock.unlock();
    }
    {
        SpinGuard guard{lock};
        (void)guard;
    }
    [[maybe_unused]] const bool reacquired = lock.try_lock();
    if (reacquired) {
        lock.unlock();
    }
}

}  // namespace crucible::concurrent
