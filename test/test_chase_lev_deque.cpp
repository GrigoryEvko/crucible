// ═══════════════════════════════════════════════════════════════════
// test_chase_lev_deque — owner+thieves work-stealing correctness.
//
// What we prove:
//
//   1. Single-thread round-trip: push N items, pop N items, get N
//      items back in LIFO order.
//   2. Capacity bound: push beyond Capacity returns false.
//   3. Empty / non-empty queries work.
//   4. **Stress test (the load-bearing one)**: 1 owner pushes N
//      distinct items while it concurrently does pop_bottom; M
//      thieves race to steal_top.  At end, the union of all
//      received items MUST equal exactly the N pushed items —
//      no item lost (item never delivered), no item duplicated
//      (delivered to two threads).  Any violation indicates a
//      memory-ordering bug in the seq_cst protocol.
//   5. Compile-time: not copyable, not movable.
//
// Item-tracking invariant proves correctness without needing a
// reference simulator: each item is a unique uint32_t marked in a
// per-item std::atomic<bool>.  The owner pushes items 0..N-1; each
// receiver (owner-take or thief-steal) marks item.  After all
// threads stop:
//   - Every bit must be set (no item lost)
//   - No bit was set twice (the marker uses CAS and the test
//     accumulates a duplicate counter; duplicate==0 mandatory)
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/ChaseLevDeque.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

// ── Compile-time checks ────────────────────────────────────────────

using TestDeque = ChaseLevDeque<uint64_t, 256>;

static_assert(!std::is_copy_constructible_v<TestDeque>,
              "ChaseLevDeque must not be copyable (Pinned contract)");
static_assert(!std::is_move_constructible_v<TestDeque>,
              "ChaseLevDeque must not be movable (interior atomics)");
static_assert(TestDeque::capacity() == 256);

// Concept enforcement: non-trivially-copyable T must be rejected.
struct NotTriviallyCopyable {
    int x;
    NotTriviallyCopyable(const NotTriviallyCopyable&) {}
};
static_assert(!DequeValue<NotTriviallyCopyable>);

// Pointer to anything is fine (8 bytes, lock-free atomic).
static_assert(DequeValue<int*>);
static_assert(DequeValue<uint64_t>);

// ── Unit: empty deque ─────────────────────────────────────────────

static void test_empty_deque() {
    ChaseLevDeque<uint64_t, 64> dq;
    assert(dq.empty_approx());
    assert(dq.size_approx() == 0);
    assert(!dq.pop_bottom().has_value());
    assert(!dq.steal_top().has_value());
    std::printf("  test_empty_deque: PASSED\n");
}

// ── Unit: single push / pop round-trip ─────────────────────────────

static void test_single_thread_lifo() {
    ChaseLevDeque<uint64_t, 64> dq;

    for (uint64_t i = 0; i < 32; ++i) {
        assert(dq.push_bottom(i));
    }
    assert(dq.size_approx() == 32);

    // pop_bottom is LIFO: receive 31, 30, 29, ..., 0
    for (uint64_t i = 0; i < 32; ++i) {
        const uint64_t expected = 31 - i;
        const auto opt = dq.pop_bottom();
        assert(opt.has_value());
        assert(*opt == expected);
    }
    assert(dq.empty_approx());
    assert(!dq.pop_bottom().has_value());

    std::printf("  test_single_thread_lifo: PASSED\n");
}

// ── Unit: single-thread steal_top is FIFO ─────────────────────────

static void test_single_thread_steal_fifo() {
    ChaseLevDeque<uint64_t, 64> dq;

    for (uint64_t i = 0; i < 32; ++i) {
        assert(dq.push_bottom(i));
    }
    // steal_top is FIFO: receive 0, 1, 2, ..., 31
    for (uint64_t i = 0; i < 32; ++i) {
        const auto opt = dq.steal_top();
        assert(opt.has_value());
        assert(*opt == i);
    }
    assert(!dq.steal_top().has_value());

    std::printf("  test_single_thread_steal_fifo: PASSED\n");
}

// ── Unit: capacity bound ──────────────────────────────────────────

static void test_capacity_bound() {
    ChaseLevDeque<uint64_t, 8> dq;
    for (uint64_t i = 0; i < 8; ++i) {
        assert(dq.push_bottom(i));
    }
    // 9th push must fail (capacity exhausted).
    assert(!dq.push_bottom(99));
    assert(dq.size_approx() == 8);

    // Pop one, then push must succeed again.
    auto popped = dq.pop_bottom();
    assert(popped.has_value());
    assert(*popped == 7);
    assert(dq.push_bottom(99));
    assert(dq.size_approx() == 8);

    std::printf("  test_capacity_bound: PASSED\n");
}

// ── Unit: interleaved push / pop / steal in one thread ────────────

static void test_interleaved_single_thread() {
    ChaseLevDeque<uint64_t, 32> dq;

    assert(dq.push_bottom(1));
    assert(dq.push_bottom(2));
    assert(dq.push_bottom(3));

    // steal_top from owner thread (allowed, just unusual)
    auto a = dq.steal_top();
    assert(a.has_value() && *a == 1);

    // pop_bottom returns 3
    auto b = dq.pop_bottom();
    assert(b.has_value() && *b == 3);

    // Now only 2 remains
    auto c = dq.steal_top();
    assert(c.has_value() && *c == 2);

    assert(dq.empty_approx());

    std::printf("  test_interleaved_single_thread: PASSED\n");
}

// ── Stress: 1 owner + N thieves with item-tracking invariant ──────
//
// The most important test.  Owner pushes items 0..N-1 over time,
// concurrently doing pop_bottom (so it sometimes acts as both
// producer and consumer); M thieves race to steal_top.  Each item
// is marked exactly once by its receiver; we then verify:
//   - All N items received (no losses)
//   - No item received twice (no duplicates)
// Either failure mode = memory-ordering bug.

static void test_stress_one_owner_n_thieves() {
    constexpr std::size_t N_ITEMS = 200'000;
    constexpr int N_THIEVES = 4;
    constexpr std::size_t CAPACITY = 1024;

    std::printf("  test_stress_one_owner_n_thieves: %zu items, %d thieves...\n",
                N_ITEMS, N_THIEVES);

    ChaseLevDeque<uint64_t, CAPACITY> dq;
    std::atomic<bool> owner_done{false};
    std::atomic<uint64_t> duplicate_count{0};
    std::atomic<uint64_t> empty_steal_count{0};

    // Per-item received-flag bitmap.  Each item index i is
    // represented by markers_[i].  Receiving an item must set the
    // corresponding bit FROM false TO true; if it was already true,
    // increment duplicate_count.
    std::vector<std::atomic<bool>> markers(N_ITEMS);
    for (auto& m : markers) m.store(false, std::memory_order_relaxed);

    auto receive = [&](uint64_t item) {
        if (item >= N_ITEMS) {
            // Item out of range — would mean uninitialized cell read.
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool prev = markers[item].exchange(true, std::memory_order_relaxed);
        if (prev) {
            // Item received twice — duplication bug.
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Owner thread: pushes 0..N-1, occasionally popping its own
    // bottom to add real concurrency to the race window.
    std::jthread owner([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N_ITEMS; ++i) {
            // Spin until push succeeds (queue full → wait for thieves).
            while (!dq.push_bottom(i)) {
                // Try to drain from our own end too, helps thieves.
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
                std::this_thread::yield();
            }
            // Periodically pop our own bottom — exercises the
            // owner-vs-thief race window on the LAST element.
            if ((i & 0x3F) == 0) {
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
            }
        }
        // After pushing all N items, drain whatever remains.
        while (auto opt = dq.pop_bottom()) {
            receive(*opt);
        }
        owner_done.store(true, std::memory_order_release);
    });

    // Thief threads: continuously steal until owner is done AND
    // deque is drained.
    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&](std::stop_token /*st*/) {
            while (true) {
                if (auto opt = dq.steal_top()) {
                    receive(*opt);
                } else {
                    empty_steal_count.fetch_add(1, std::memory_order_relaxed);
                    if (owner_done.load(std::memory_order_acquire)
                        && dq.empty_approx())
                    {
                        // Final drain: try one more steal in case
                        // the empty_approx was stale.
                        if (auto last = dq.steal_top()) {
                            receive(*last);
                        } else {
                            break;
                        }
                    }
                }
            }
        });
    }

    owner = std::jthread{};
    thieves.clear();

    // Verify all items received exactly once.
    std::size_t missing = 0;
    for (std::size_t i = 0; i < N_ITEMS; ++i) {
        if (!markers[i].load(std::memory_order_relaxed)) {
            ++missing;
        }
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);
    const uint64_t empty_steals = empty_steal_count.load(std::memory_order_relaxed);

    std::printf("    items: %zu, missing: %zu, duplicates: %llu\n"
                "    empty steal attempts: %llu (informational)\n",
                N_ITEMS, missing,
                static_cast<unsigned long long>(dup),
                static_cast<unsigned long long>(empty_steals));

    assert(missing == 0
        && "item lost — owner pushed but no receiver marked it");
    assert(dup == 0
        && "item duplicated — owner-vs-thief race resolution is broken");

    std::printf("  test_stress_one_owner_n_thieves: PASSED\n");
}

// ── Stress: heavy contention (many thieves on small deque) ────────
//
// Smaller deque + more thieves = higher steal contention.  The CAS
// on top will fail more often, exercising the retry path.

static void test_stress_high_contention() {
    constexpr std::size_t N_ITEMS = 50'000;
    constexpr int N_THIEVES = 8;
    constexpr std::size_t CAPACITY = 32;

    std::printf("  test_stress_high_contention: %zu items, %d thieves, capacity %zu...\n",
                N_ITEMS, N_THIEVES, CAPACITY);

    ChaseLevDeque<uint32_t, CAPACITY> dq;
    std::atomic<bool> owner_done{false};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::atomic<bool>> markers(N_ITEMS);
    for (auto& m : markers) m.store(false, std::memory_order_relaxed);

    auto receive = [&](uint32_t item) {
        if (item >= N_ITEMS) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool prev = markers[item].exchange(true, std::memory_order_relaxed);
        if (prev) {
            duplicate_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::jthread owner([&](std::stop_token /*st*/) {
        for (uint32_t i = 0; i < N_ITEMS; ++i) {
            while (!dq.push_bottom(i)) {
                if (auto opt = dq.pop_bottom()) {
                    receive(*opt);
                }
                std::this_thread::yield();
            }
        }
        while (auto opt = dq.pop_bottom()) {
            receive(*opt);
        }
        owner_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> thieves;
    for (int t = 0; t < N_THIEVES; ++t) {
        thieves.emplace_back([&](std::stop_token /*st*/) {
            while (true) {
                if (auto opt = dq.steal_top()) {
                    receive(*opt);
                } else if (owner_done.load(std::memory_order_acquire)
                           && dq.empty_approx())
                {
                    if (auto last = dq.steal_top()) {
                        receive(*last);
                    } else {
                        break;
                    }
                }
            }
        });
    }

    owner = std::jthread{};
    thieves.clear();

    std::size_t missing = 0;
    for (std::size_t i = 0; i < N_ITEMS; ++i) {
        if (!markers[i].load(std::memory_order_relaxed)) ++missing;
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    std::printf("    items: %zu, missing: %zu, duplicates: %llu\n",
                N_ITEMS, missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0 && "item lost under high contention");
    assert(dup == 0 && "item duplicated under high contention");

    std::printf("  test_stress_high_contention: PASSED\n");
}

// ── Stress: pointer payload ────────────────────────────────────────

static void test_pointer_payload() {
    // T = int* exercises the lock-free atomic guarantee for
    // pointer-sized values (the common case for compile pool
    // jobs).
    ChaseLevDeque<int*, 64> dq;

    int values[10];
    for (int i = 0; i < 10; ++i) {
        values[i] = i * 100;
        assert(dq.push_bottom(&values[i]));
    }

    // Pop bottom: receive &values[9], 8, 7, ...
    for (int i = 9; i >= 0; --i) {
        auto opt = dq.pop_bottom();
        assert(opt.has_value());
        assert(*opt == &values[i]);
        assert(**opt == i * 100);
    }

    std::printf("  test_pointer_payload: PASSED\n");
}

int main() {
    std::printf("test_chase_lev_deque:\n");

    test_empty_deque();
    test_single_thread_lifo();
    test_single_thread_steal_fifo();
    test_capacity_bound();
    test_interleaved_single_thread();
    test_pointer_payload();
    test_stress_one_owner_n_thieves();
    test_stress_high_contention();

    std::printf("test_chase_lev_deque: ALL PASSED\n");
    return 0;
}
