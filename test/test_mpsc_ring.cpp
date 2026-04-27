// ═══════════════════════════════════════════════════════════════════
// test_mpsc_ring — multi-producer single-consumer queue correctness.
//
// What we prove:
//
//   1. Single-thread enqueue/dequeue round-trip (producer == consumer
//      for setup; not the supported pattern but useful for unit
//      testing the per-cell protocol).
//   2. Capacity bound: try_push beyond Capacity returns false until
//      consumer drains.
//   3. FIFO ordering for single producer.
//   4. **Stress test (the load-bearing one)**: M producers, 1
//      consumer.  Each producer pushes a distinct sequence of
//      items; consumer dequeues into a per-(producer, seq) marker
//      bitmap.  Verify every (producer, seq) received exactly
//      once.
//   5. Compile-time: not copyable, not movable.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/MpscRing.h>

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

using TestRing = MpscRing<uint64_t, 256>;

static_assert(!std::is_copy_constructible_v<TestRing>,
              "MpscRing must not be copyable (Pinned contract)");
static_assert(!std::is_move_constructible_v<TestRing>,
              "MpscRing must not be movable (interior atomics)");
static_assert(TestRing::capacity() == 256);

// ── Unit: empty queue ─────────────────────────────────────────────

static void test_empty_queue() {
    MpscRing<uint64_t, 16> q;
    assert(q.empty_approx());
    assert(!q.try_pop().has_value());
    std::printf("  test_empty_queue: PASSED\n");
}

// ── Unit: single-thread FIFO round-trip ───────────────────────────

static void test_single_thread_fifo() {
    MpscRing<uint64_t, 64> q;

    for (uint64_t i = 0; i < 32; ++i) {
        assert(q.try_push(i));
    }
    // FIFO: receive 0, 1, 2, ..., 31
    for (uint64_t i = 0; i < 32; ++i) {
        const auto opt = q.try_pop();
        assert(opt.has_value());
        assert(*opt == i);
    }
    assert(q.empty_approx());
    assert(!q.try_pop().has_value());

    std::printf("  test_single_thread_fifo: PASSED\n");
}

// ── Unit: capacity bound, then drain re-enables ───────────────────

static void test_capacity_bound() {
    MpscRing<uint64_t, 8> q;

    for (uint64_t i = 0; i < 8; ++i) {
        assert(q.try_push(i));
    }
    // 9th push must fail.
    assert(!q.try_push(99));

    // Drain one, then push must succeed.
    auto first = q.try_pop();
    assert(first.has_value() && *first == 0);
    assert(q.try_push(99));
    assert(!q.try_push(100));  // full again

    // Drain remaining 7 + the late-pushed 99.
    for (uint64_t expected : {1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL, 99ULL}) {
        auto opt = q.try_pop();
        assert(opt.has_value());
        assert(*opt == expected);
    }
    assert(q.empty_approx());

    std::printf("  test_capacity_bound: PASSED\n");
}

// ── Unit: wrap-around ─────────────────────────────────────────────
//
// Push N, drain N, push N more — the cell sequences must cleanly
// transition from "drained" back to "ready for next round
// producer".

static void test_wrap_around() {
    MpscRing<uint64_t, 4> q;

    for (int round = 0; round < 5; ++round) {
        for (uint64_t i = 0; i < 4; ++i) {
            const uint64_t value = static_cast<uint64_t>(round) * 100 + i;
            assert(q.try_push(value));
        }
        for (uint64_t i = 0; i < 4; ++i) {
            const uint64_t expected = static_cast<uint64_t>(round) * 100 + i;
            const auto opt = q.try_pop();
            assert(opt.has_value());
            assert(*opt == expected);
        }
    }

    std::printf("  test_wrap_around: PASSED\n");
}

// ── Stress: M producers + 1 consumer ──────────────────────────────
//
// Item-tracking invariant: each item encodes (producer_id, seq_id);
// consumer marks (producer_id, seq_id) into a bitmap.  After all
// threads stop:
//   - Every expected (id, seq) marked → no item lost
//   - No bit was set twice → no item duplicated

static void test_stress_m_producers_one_consumer() {
    constexpr std::size_t N_PRODUCERS = 4;
    constexpr std::size_t N_PER_PRODUCER = 50'000;
    constexpr std::size_t CAPACITY = 1024;

    std::printf("  test_stress_m_producers_one_consumer: %zu producers × %zu items, capacity %zu...\n",
                N_PRODUCERS, N_PER_PRODUCER, CAPACITY);

    MpscRing<uint64_t, CAPACITY> q;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<uint64_t> duplicate_count{0};

    // Per-(producer, seq) marker bitmap.  Item encoding:
    //   high 16 bits = producer_id
    //   low 48 bits  = seq_id
    // Producer i pushes items {(i << 48) | 0, (i << 48) | 1, ..., (i << 48) | (N-1)}.
    std::vector<std::vector<std::atomic<bool>>> markers(N_PRODUCERS);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v) m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t producer_id, uint64_t seq) -> uint64_t {
        return (static_cast<uint64_t>(producer_id) << 48) | seq;
    };
    auto decode_producer = [](uint64_t item) -> std::size_t {
        // size_t == uint64_t on Linux x86-64 — explicit cast would
        // be flagged useless; conversion is implicit and exact.
        return item >> 48;
    };
    auto decode_seq = [](uint64_t item) -> uint64_t {
        return item & ((uint64_t{1} << 48) - 1);
    };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                const uint64_t item = encode(p, s);
                while (!q.try_push(item)) {
                    std::this_thread::yield();
                }
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    std::jthread consumer([&](std::stop_token /*st*/) {
        std::size_t received = 0;
        const std::size_t total = N_PRODUCERS * N_PER_PRODUCER;
        while (received < total) {
            if (auto opt = q.try_pop()) {
                const std::size_t p = decode_producer(*opt);
                const uint64_t s = decode_seq(*opt);
                if (p >= N_PRODUCERS || s >= N_PER_PRODUCER) {
                    duplicate_count.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                bool prev = markers[p][s].exchange(true, std::memory_order_relaxed);
                if (prev) {
                    duplicate_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++received;
                }
            } else {
                // Empty — only break if all producers done AND
                // we've truly received everything.
                if (producers_done.load(std::memory_order_acquire) == N_PRODUCERS) {
                    // Final drain attempt — produced items might
                    // still be in flight after producers' done flag.
                    if (auto last = q.try_pop()) {
                        const std::size_t p = decode_producer(*last);
                        const uint64_t s = decode_seq(*last);
                        if (p < N_PRODUCERS && s < N_PER_PRODUCER) {
                            bool prev = markers[p][s].exchange(true, std::memory_order_relaxed);
                            if (prev) {
                                duplicate_count.fetch_add(1, std::memory_order_relaxed);
                            } else {
                                ++received;
                            }
                        }
                    } else if (received == total) {
                        break;
                    } else {
                        std::this_thread::yield();
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        }
    });

    producers.clear();
    consumer = std::jthread{};

    // Verify all items received exactly once.
    std::size_t missing = 0;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        for (std::size_t s = 0; s < N_PER_PRODUCER; ++s) {
            if (!markers[p][s].load(std::memory_order_relaxed)) {
                ++missing;
            }
        }
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n",
                N_PRODUCERS * N_PER_PRODUCER,
                missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0
        && "item lost — producer pushed but consumer never received");
    assert(dup == 0
        && "item duplicated — single-consumer protocol is broken");

    std::printf("  test_stress_m_producers_one_consumer: PASSED\n");
}

// ── Stress: heavy producer contention with small queue ────────────
//
// Many producers contending for few slots — exercises the CAS
// retry path in try_push.

static void test_stress_high_contention() {
    constexpr std::size_t N_PRODUCERS = 8;
    constexpr std::size_t N_PER_PRODUCER = 10'000;
    constexpr std::size_t CAPACITY = 16;

    std::printf("  test_stress_high_contention: %zu producers × %zu items, capacity %zu...\n",
                N_PRODUCERS, N_PER_PRODUCER, CAPACITY);

    MpscRing<uint64_t, CAPACITY> q;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::vector<std::atomic<bool>>> markers(N_PRODUCERS);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v) m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t p, uint64_t s) {
        return (static_cast<uint64_t>(p) << 48) | s;
    };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                while (!q.try_push(encode(p, s))) {
                    std::this_thread::yield();
                }
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    std::jthread consumer([&](std::stop_token /*st*/) {
        std::size_t received = 0;
        const std::size_t total = N_PRODUCERS * N_PER_PRODUCER;
        while (received < total) {
            if (auto opt = q.try_pop()) {
                const std::size_t p = *opt >> 48;
                const uint64_t s = *opt & ((uint64_t{1} << 48) - 1);
                if (p >= N_PRODUCERS || s >= N_PER_PRODUCER) {
                    duplicate_count.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                bool prev = markers[p][s].exchange(true, std::memory_order_relaxed);
                if (prev) {
                    duplicate_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++received;
                }
            } else if (producers_done.load(std::memory_order_acquire) == N_PRODUCERS
                       && received == total) {
                break;
            } else {
                // Tiny pause to avoid pegging the CPU on empty polling.
                std::this_thread::yield();
            }
        }
    });

    producers.clear();
    consumer = std::jthread{};

    std::size_t missing = 0;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        for (std::size_t s = 0; s < N_PER_PRODUCER; ++s) {
            if (!markers[p][s].load(std::memory_order_relaxed)) ++missing;
        }
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n",
                N_PRODUCERS * N_PER_PRODUCER,
                missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0 && "item lost under high producer contention");
    assert(dup == 0 && "item duplicated under high producer contention");

    std::printf("  test_stress_high_contention: PASSED\n");
}

// ── Batched API correctness (try_push_batch / try_pop_batch) ──────
//
// Validates the batched primitives ship the expected behavior:
//
//   1. Single-shot batched round-trip — all-or-nothing semantics.
//   2. Wrap-around within a batch (buffer end → start in one push).
//   3. Mixed single + batched — correctness across both code paths.
//   4. Many-batch drain — sustained operation.
//   5. Multi-producer batched stress — 4 producers each pushing
//      via try_push_batch, single consumer draining via try_pop_batch.
//      Exact-once delivery preserved.

static void test_batched_round_trip() {
    std::printf("  test_batched_round_trip: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 256>>();

    constexpr size_t N = 64;
    std::array<uint64_t, N> tx{};
    for (size_t i = 0; i < N; ++i) tx[i] = 1000 + i;

    // Push the whole batch in one call.
    const size_t pushed = ring->try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == N && "batch push must be all-or-nothing");

    // Drain via batched pop.
    std::array<uint64_t, N> rx{};
    const size_t popped = ring->try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == N);

    // FIFO order preserved.
    for (size_t i = 0; i < N; ++i) {
        assert(rx[i] == 1000 + i && "FIFO order broken");
    }
    std::printf("PASSED\n");
}

static void test_batched_wrap_around() {
    std::printf("  test_batched_wrap_around: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 16>>();

    // Push 12 single, drain all → head=12, tail=12, cells 0..11 in
    // "drained" state with sequence = i + 16.
    for (uint64_t i = 0; i < 12; ++i) assert(ring->try_push(i));
    for (uint64_t i = 0; i < 12; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }

    // Now push a batch of 8 — wraps around end of buffer (12..19,
    // physical cells 12,13,14,15,0,1,2,3).
    std::array<uint64_t, 8> tx{};
    for (size_t i = 0; i < 8; ++i) tx[i] = 100 + i;
    const size_t pushed = ring->try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == 8 && "wrap-around batch must succeed");

    std::array<uint64_t, 8> rx{};
    const size_t popped = ring->try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == 8);
    for (size_t i = 0; i < 8; ++i) {
        assert(rx[i] == 100 + i);
    }
    std::printf("PASSED\n");
}

static void test_batched_full_rejection() {
    std::printf("  test_batched_full_rejection: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 16>>();

    // Fill to capacity via singles.
    for (uint64_t i = 0; i < 16; ++i) assert(ring->try_push(i));

    // A batch of 4 must fail (queue full) — and NO partial fill.
    std::array<uint64_t, 4> tx{99, 99, 99, 99};
    const size_t pushed = ring->try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == 0 && "full queue must reject batch entirely");

    // Verify by popping all 16 and checking the sequence is the
    // original 0..15 with no 99s mixed in.
    for (uint64_t i = 0; i < 16; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }
    std::printf("PASSED\n");
}

static void test_batched_mixed_with_singles() {
    std::printf("  test_batched_mixed_with_singles: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 256>>();

    // Push 10 singles, then a batch of 32, then 10 more singles.
    for (uint64_t i = 0; i < 10; ++i) assert(ring->try_push(i));

    std::array<uint64_t, 32> tx{};
    for (size_t i = 0; i < 32; ++i) tx[i] = 100 + i;
    assert(ring->try_push_batch(std::span<const uint64_t>(tx)) == 32);

    for (uint64_t i = 0; i < 10; ++i) assert(ring->try_push(200 + i));

    // Drain via mix of single + batched.
    for (uint64_t i = 0; i < 10; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }
    std::array<uint64_t, 32> rx{};
    assert(ring->try_pop_batch(std::span<uint64_t>(rx)) == 32);
    for (size_t i = 0; i < 32; ++i) assert(rx[i] == 100 + i);
    for (uint64_t i = 0; i < 10; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == 200 + i);
    }
    std::printf("PASSED\n");
}

static void test_batched_multi_producer_stress() {
    constexpr size_t M = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 50000;
    constexpr size_t BATCH = 16;
    constexpr size_t TOTAL = M * ITEMS_PER_PRODUCER;
    std::printf("  test_batched_multi_producer_stress: %zu producers × "
                "%zu items via batch<%zu>...\n",
                M, ITEMS_PER_PRODUCER, BATCH);

    auto ring = std::make_unique<MpscRing<uint64_t, 1024>>();
    std::vector<std::atomic<int>> seen(TOTAL);
    std::atomic<bool> start{false};
    std::atomic<size_t> consumed{0};

    auto encode = [](size_t producer_id, size_t seq) {
        return static_cast<uint64_t>(producer_id) * ITEMS_PER_PRODUCER + seq;
    };

    std::vector<std::jthread> producers;
    for (size_t p = 0; p < M; ++p) {
        producers.emplace_back([&ring, &start, &encode, p, BATCH](
                std::stop_token) {
            while (!start.load(std::memory_order_acquire)) { /* spin */ }

            std::array<uint64_t, BATCH> buf{};
            for (size_t base = 0; base < ITEMS_PER_PRODUCER; base += BATCH) {
                const size_t n = std::min(BATCH, ITEMS_PER_PRODUCER - base);
                for (size_t i = 0; i < n; ++i) {
                    buf[i] = encode(p, base + i);
                }
                size_t pushed = 0;
                while (pushed < n) {
                    const size_t r = ring->try_push_batch(
                        std::span<const uint64_t>(buf.data() + pushed,
                                                   n - pushed));
                    if (r > 0) {
                        pushed += r;
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
        });
    }

    std::jthread consumer([&ring, &seen, &consumed, &start](std::stop_token) {
        while (!start.load(std::memory_order_acquire)) { /* spin */ }
        std::array<uint64_t, 32> buf{};
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            const size_t n = ring->try_pop_batch(std::span<uint64_t>(buf));
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
    producers.clear();        // join
    consumer.join();

    int missing = 0, dup = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) dup += c - 1;
    }
    std::printf("    expected: %zu, missing: %d, duplicates: %d\n",
                TOTAL, missing, dup);
    assert(missing == 0 && "item lost under batched contention");
    assert(dup == 0 && "item duplicated under batched contention");
    std::printf("  test_batched_multi_producer_stress: PASSED\n");
}

int main() {
    std::printf("test_mpsc_ring:\n");

    test_empty_queue();
    test_single_thread_fifo();
    test_capacity_bound();
    test_wrap_around();
    test_stress_m_producers_one_consumer();
    test_stress_high_contention();

    // Batched API
    test_batched_round_trip();
    test_batched_wrap_around();
    test_batched_full_rejection();
    test_batched_mixed_with_singles();
    test_batched_multi_producer_stress();

    std::printf("test_mpsc_ring: ALL PASSED\n");
    return 0;
}
