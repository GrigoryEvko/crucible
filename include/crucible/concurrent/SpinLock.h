#pragma once

#include <crucible/Platform.h>

#include <atomic>

namespace crucible::concurrent {

// fixy-A5-022: alignas(64) is load-bearing.  SpinLock is routinely embedded
// in arrays (`std::array<SpinLock, M> producer_locks_` in AdaptiveScheduler
// MPSC/MPMC sharded grids), and a 1-byte std::atomic_flag would otherwise let
// every contending producer pin its lock to the same 64-byte line — the
// classic 40× false-sharing trap (CLAUDE.md §VIII).  Centralizing the
// alignment on the primitive removes the per-embed-site discipline burden
// (BackgroundThread.h:150 had to apply alignas(64) externally precisely
// because SpinLock itself didn't).
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

class SpinGuard {
public:
    explicit SpinGuard(SpinLock& lock) noexcept
        : lock_{lock} {
        lock_.lock();
    }

    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;

    ~SpinGuard() noexcept {
        lock_.unlock();
    }

private:
    SpinLock& lock_;
};

}  // namespace crucible::concurrent
