#include <crucible/concurrent/SpinLock.h>

#include "test_assert.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <thread>
#include <type_traits>

namespace conc = crucible::concurrent;

namespace {

// fixy-A5-022 regression: SpinLock has alignas(64), so std::array<SpinLock, N>
// must place every element on a distinct cache line.  Without the fix, an
// 8-element array fit inside a single 64-byte line and false-sharing made
// adjacent producers contend on every test_and_set.

void test_spinlock_layout_invariants() {
    static_assert(alignof(conc::SpinLock) >= 64,
                  "SpinLock must be cache-line-aligned");
    static_assert(sizeof(conc::SpinLock) >= 64,
                  "SpinLock occupies a full cache line");
    static_assert(!std::is_copy_constructible_v<conc::SpinLock>);
    static_assert(!std::is_move_constructible_v<conc::SpinLock>);
    static_assert(!std::is_copy_assignable_v<conc::SpinLock>);
    static_assert(!std::is_move_assignable_v<conc::SpinLock>);

    std::array<conc::SpinLock, 8> locks{};
    constexpr std::uintptr_t LINE = 64;
    for (std::size_t i = 0; i < locks.size(); ++i) {
        const auto addr = std::bit_cast<std::uintptr_t>(&locks[i]);
        assert((addr % LINE) == 0);
        if (i > 0) {
            const auto prev_addr =
                std::bit_cast<std::uintptr_t>(&locks[i - 1]);
            assert(addr - prev_addr >= LINE);
        }
    }

    std::printf("  test_spinlock_layout_invariants: PASSED\n");
}

// FIXY-U-085: try_lock completes the Lockable concept.  Two assertions:
//   (1) try_lock succeeds when the lock is unheld and acquires it (subsequent
//       try_lock must fail until unlock).
//   (2) try_lock fails when another holder owns the lock.
// Both paths verify that try_lock does NOT spin — it returns immediately.
void test_spinlock_try_lock_semantics() {
    conc::SpinLock lock;

    const bool first = lock.try_lock();
    assert(first);
    const bool second_while_held = lock.try_lock();
    assert(!second_while_held);
    lock.unlock();

    const bool third_after_unlock = lock.try_lock();
    assert(third_after_unlock);
    lock.unlock();

    // Cross-thread try_lock: a contender must observe failure while the
    // primary thread holds the lock.
    {
        conc::SpinGuard primary{lock};
        std::atomic<bool> contender_saw_failure{false};
        std::thread contender{[&]() noexcept {
            contender_saw_failure.store(!lock.try_lock(),
                                        std::memory_order_release);
        }};
        contender.join();
        assert(contender_saw_failure.load(std::memory_order_acquire));
    }

    std::printf("  test_spinlock_try_lock_semantics: PASSED\n");
}

// FIXY-U-085 consolidation regression: the two ex-private SpinGuards in
// ConnectionPoolRuntime + BackpressureRuntime each composed differently with
// the underlying flag — one had _mm_pause, one didn't; one had alignas(64) at
// the embed site, one didn't.  The canonical primitive collapses both axes.
// This test pins the composition contract that downstream sites depend on:
// embedding a SpinLock as a struct field inherits alignas(64) automatically.
void test_spinlock_embed_inherits_alignment() {
    struct Embedder {
        std::uint64_t prefix = 0;
        conc::SpinLock lock;
        std::uint64_t suffix = 0;
    };

    static_assert(alignof(Embedder) >= 64,
                  "Embedder must inherit alignment from member SpinLock");
    static_assert(sizeof(Embedder) >= 128,
                  "Embedder cannot fit in one cache line if SpinLock occupies "
                  "its own line — the trailing suffix must land beyond byte 64");

    Embedder e;
    const auto base = std::bit_cast<std::uintptr_t>(&e);
    const auto lock_addr = std::bit_cast<std::uintptr_t>(&e.lock);
    const auto suffix_addr = std::bit_cast<std::uintptr_t>(&e.suffix);

    assert((lock_addr % 64) == 0);
    assert(lock_addr >= base + 64);   // pushed past prefix to next line
    assert(suffix_addr >= lock_addr + 64);  // suffix on a third line

    std::printf("  test_spinlock_embed_inherits_alignment: PASSED\n");
}

void test_spinlock_mutual_exclusion_under_contention() {
    // Soundness sanity: under heavy contention, the mutex contract still
    // holds — exactly one thread inside the critical section at any time.
    // Eight producers race; counter increments are unguarded across the
    // wait/notify boundary, but the spin lock must serialize them.
    constexpr std::size_t kThreads = 8;
    constexpr std::size_t kIterationsPerThread = 1'000;

    conc::SpinLock lock;
    std::int64_t guarded_counter = 0;
    std::atomic<int> max_concurrent_inside{0};
    std::atomic<int> inside_now{0};

    std::array<std::thread, kThreads> workers;
    for (std::size_t t = 0; t < kThreads; ++t) {
        workers[t] = std::thread{[&]() noexcept {
            for (std::size_t i = 0; i < kIterationsPerThread; ++i) {
                conc::SpinGuard guard{lock};
                const int cur = inside_now.fetch_add(1,
                    std::memory_order_relaxed) + 1;
                int prev_max = max_concurrent_inside.load(
                    std::memory_order_relaxed);
                while (cur > prev_max &&
                       !max_concurrent_inside.compare_exchange_weak(
                           prev_max, cur, std::memory_order_relaxed)) {
                }
                ++guarded_counter;
                inside_now.fetch_sub(1, std::memory_order_relaxed);
            }
        }};
    }
    for (auto& w : workers) {
        w.join();
    }

    assert(guarded_counter ==
                         std::int64_t{kThreads} * kIterationsPerThread);
    assert(max_concurrent_inside.load() == 1);

    std::printf("  test_spinlock_mutual_exclusion_under_contention: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_concurrent_spinlock:\n");
    test_spinlock_layout_invariants();
    test_spinlock_try_lock_semantics();
    test_spinlock_embed_inherits_alignment();
    test_spinlock_mutual_exclusion_under_contention();
    conc::spinlock_runtime_smoke_test();
    std::printf("test_concurrent_spinlock: all PASSED\n");
    return 0;
}
