#pragma once

#include <crucible/Platform.h>

#include <atomic>

namespace crucible::concurrent {

class SpinLock {
public:
    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

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
