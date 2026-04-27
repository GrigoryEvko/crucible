// ═══════════════════════════════════════════════════════════════════
// test_bitmap_mpsc_ring — MPSC ring with out-of-band bitmap metadata.
//
// Design departure from Vyukov per-cell sequence (see header):
//   * Cells are PURE T (no per-cell metadata)
//   * Ready signal lives in separate bitmap (1 bit per cell)
//   * Cross-round synchronization via capacity gate, not per-cell seq
//
// Tests prove:
//   1. Single-thread round-trip
//   2. Capacity bound + drain re-enables
//   3. Wrap-around (cells span buffer end)
//   4. Empty queue returns nullopt
//   5. Batched API (push_batch / pop_batch) round-trip
//   6. Batched wrap-around
//   7. Batched full rejection (all-or-nothing)
//   8. Stress: N producers × 1 consumer, exactly-once delivery
//   9. Batched stress: N producers × 1 consumer via batched API
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/BitmapMpscRing.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

// Compile-time checks
using TestRing = BitmapMpscRing<uint64_t, 256>;
static_assert(!std::is_copy_constructible_v<TestRing>);
static_assert(!std::is_move_constructible_v<TestRing>);
static_assert(TestRing::capacity() == 256);

static void test_empty_queue() {
    BitmapMpscRing<uint64_t, 64> q;
    assert(q.empty_approx());
    assert(!q.try_pop().has_value());
    std::printf("  test_empty_queue: PASSED\n");
}

static void test_single_thread_fifo() {
    BitmapMpscRing<uint64_t, 64> q;
    for (uint64_t i = 0; i < 32; ++i) {
        assert(q.try_push(i));
    }
    for (uint64_t i = 0; i < 32; ++i) {
        auto opt = q.try_pop();
        assert(opt && *opt == i);
    }
    assert(!q.try_pop().has_value());
    std::printf("  test_single_thread_fifo: PASSED\n");
}

static void test_capacity_bound() {
    BitmapMpscRing<uint64_t, 64> q;
    for (uint64_t i = 0; i < 64; ++i) {
        assert(q.try_push(i));
    }
    assert(!q.try_push(99));  // full

    auto first = q.try_pop();
    assert(first && *first == 0);
    assert(q.try_push(99));
    assert(!q.try_push(100));

    // Drain remaining
    for (uint64_t i = 1; i < 64; ++i) {
        auto opt = q.try_pop();
        assert(opt && *opt == i);
    }
    auto last = q.try_pop();
    assert(last && *last == 99);
    std::printf("  test_capacity_bound: PASSED\n");
}

static void test_wrap_around() {
    BitmapMpscRing<uint64_t, 64> q;
    // Push and drain multiple rounds — must reach round 5+
    for (int round = 0; round < 5; ++round) {
        for (uint64_t i = 0; i < 64; ++i) {
            const uint64_t value = static_cast<uint64_t>(round) * 1000 + i;
            assert(q.try_push(value));
        }
        for (uint64_t i = 0; i < 64; ++i) {
            const uint64_t expected = static_cast<uint64_t>(round) * 1000 + i;
            auto opt = q.try_pop();
            assert(opt && *opt == expected);
        }
    }
    std::printf("  test_wrap_around: PASSED\n");
}

static void test_batched_round_trip() {
    BitmapMpscRing<uint64_t, 256> q;
    constexpr size_t N = 64;
    std::array<uint64_t, N> tx{};
    for (size_t i = 0; i < N; ++i) tx[i] = 1000 + i;

    const size_t pushed = q.try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == N);

    std::array<uint64_t, N> rx{};
    const size_t popped = q.try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == N);
    for (size_t i = 0; i < N; ++i) {
        assert(rx[i] == 1000 + i);
    }
    std::printf("  test_batched_round_trip: PASSED\n");
}

static void test_batched_wrap_around() {
    BitmapMpscRing<uint64_t, 64> q;
    // Push 50, drain all → head=50, tail=50.  Cells 0..49 have bits
    // set then cleared; cells 50..63 untouched.
    for (uint64_t i = 0; i < 50; ++i) assert(q.try_push(i));
    for (uint64_t i = 0; i < 50; ++i) {
        auto opt = q.try_pop();
        assert(opt && *opt == i);
    }

    // Now push a batch of 30 — wraps from cell 50 to cell 15
    // (positions 50..79, cells 50,51,...,63,0,1,...,15).
    std::array<uint64_t, 30> tx{};
    for (size_t i = 0; i < 30; ++i) tx[i] = 100 + i;
    const size_t pushed = q.try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == 30);

    std::array<uint64_t, 30> rx{};
    const size_t popped = q.try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == 30);
    for (size_t i = 0; i < 30; ++i) {
        assert(rx[i] == 100 + i);
    }
    std::printf("  test_batched_wrap_around: PASSED\n");
}

static void test_batched_full_rejection() {
    BitmapMpscRing<uint64_t, 64> q;
    for (uint64_t i = 0; i < 64; ++i) assert(q.try_push(i));

    std::array<uint64_t, 4> tx{99, 99, 99, 99};
    const size_t pushed = q.try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == 0);  // full, no partial fill

    // Verify the original 0..63 are still there in order.
    for (uint64_t i = 0; i < 64; ++i) {
        auto opt = q.try_pop();
        assert(opt && *opt == i);
    }
    std::printf("  test_batched_full_rejection: PASSED\n");
}

static void test_batched_partial_drain() {
    // try_pop_batch should drain only the contiguous READY prefix.
    // Test: push 10 items, request batch of 50 — should get 10.
    BitmapMpscRing<uint64_t, 64> q;
    for (uint64_t i = 0; i < 10; ++i) assert(q.try_push(i));

    std::array<uint64_t, 50> rx{};
    const size_t popped = q.try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == 10);
    for (size_t i = 0; i < 10; ++i) assert(rx[i] == i);
    std::printf("  test_batched_partial_drain: PASSED\n");
}

static void test_stress_m_producers_one_consumer() {
    constexpr size_t M = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 50000;
    constexpr size_t TOTAL = M * ITEMS_PER_PRODUCER;
    std::printf("  test_stress_m_producers_one_consumer: %zu producers × "
                "%zu items, capacity 1024...\n", M, ITEMS_PER_PRODUCER);

    BitmapMpscRing<uint64_t, 1024> q;
    std::vector<std::atomic<int>> seen(TOTAL);
    std::atomic<bool> start{false};
    std::atomic<size_t> consumed{0};

    auto encode = [](size_t producer_id, size_t seq) {
        return static_cast<uint64_t>(producer_id) * ITEMS_PER_PRODUCER + seq;
    };

    std::vector<std::jthread> producers;
    for (size_t p = 0; p < M; ++p) {
        producers.emplace_back([&q, &start, &encode, p](std::stop_token) {
            while (!start.load(std::memory_order_acquire)) {}
            for (size_t s = 0; s < ITEMS_PER_PRODUCER; ++s) {
                while (!q.try_push(encode(p, s))) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::jthread consumer([&q, &seen, &consumed, &start](std::stop_token) {
        while (!start.load(std::memory_order_acquire)) {}
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            if (auto v = q.try_pop()) {
                seen[*v].fetch_add(1, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    start.store(true, std::memory_order_release);
    producers.clear();  // join
    consumer.join();

    int missing = 0, dup = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) dup += c - 1;
    }
    std::printf("    expected: %zu, missing: %d, duplicates: %d\n",
                TOTAL, missing, dup);
    assert(missing == 0 && "item lost under M-producer stress");
    assert(dup == 0 && "item duplicated under M-producer stress");
    std::printf("  test_stress_m_producers_one_consumer: PASSED\n");
}

static void test_batched_multi_producer_stress() {
    constexpr size_t M = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 50000;
    constexpr size_t BATCH = 16;
    constexpr size_t TOTAL = M * ITEMS_PER_PRODUCER;
    std::printf("  test_batched_multi_producer_stress: %zu producers × %zu "
                "items via batch<%zu>...\n", M, ITEMS_PER_PRODUCER, BATCH);

    BitmapMpscRing<uint64_t, 1024> q;
    std::vector<std::atomic<int>> seen(TOTAL);
    std::atomic<bool> start{false};
    std::atomic<size_t> consumed{0};

    auto encode = [](size_t p, size_t s) {
        return static_cast<uint64_t>(p) * ITEMS_PER_PRODUCER + s;
    };

    std::vector<std::jthread> producers;
    for (size_t p = 0; p < M; ++p) {
        producers.emplace_back([&q, &start, &encode, p, BATCH](std::stop_token) {
            while (!start.load(std::memory_order_acquire)) {}
            std::array<uint64_t, BATCH> buf{};
            for (size_t base = 0; base < ITEMS_PER_PRODUCER; base += BATCH) {
                const size_t n = std::min(BATCH, ITEMS_PER_PRODUCER - base);
                for (size_t i = 0; i < n; ++i) buf[i] = encode(p, base + i);
                size_t pushed = 0;
                while (pushed < n) {
                    const size_t r = q.try_push_batch(
                        std::span<const uint64_t>(buf.data() + pushed,
                                                   n - pushed));
                    if (r > 0) pushed += r;
                    else std::this_thread::yield();
                }
            }
        });
    }

    std::jthread consumer([&q, &seen, &consumed, &start](std::stop_token) {
        while (!start.load(std::memory_order_acquire)) {}
        std::array<uint64_t, 32> buf{};
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            const size_t n = q.try_pop_batch(std::span<uint64_t>(buf));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                seen[buf[i]].fetch_add(1, std::memory_order_relaxed);
            }
            consumed.fetch_add(n, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);
    producers.clear();
    consumer.join();

    int missing = 0, dup = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) dup += c - 1;
    }
    std::printf("    expected: %zu, missing: %d, duplicates: %d\n",
                TOTAL, missing, dup);
    assert(missing == 0 && "item lost under batched stress");
    assert(dup == 0 && "item duplicated under batched stress");
    std::printf("  test_batched_multi_producer_stress: PASSED\n");
}

int main() {
    std::printf("test_bitmap_mpsc_ring:\n");

    test_empty_queue();
    test_single_thread_fifo();
    test_capacity_bound();
    test_wrap_around();
    test_batched_round_trip();
    test_batched_wrap_around();
    test_batched_full_rejection();
    test_batched_partial_drain();
    test_stress_m_producers_one_consumer();
    test_batched_multi_producer_stress();

    std::printf("test_bitmap_mpsc_ring: ALL PASSED\n");
    return 0;
}
