// The single-threaded tests drive producer and consumer from one
// thread.  That is not the supported pattern, but it isolates the
// per-cell protocol from thread scheduling.

#include <crucible/concurrent/MpscRing.h>

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

using TestRing = MpscRing<uint64_t, 256>;

static_assert(!std::is_copy_constructible_v<TestRing>, "MpscRing must not be copyable (Pinned contract)");
static_assert(!std::is_move_constructible_v<TestRing>, "MpscRing must not be movable (interior atomics)");
static_assert(TestRing::capacity() == 256);

static void test_empty_queue() {
    MpscRing<uint64_t, 16> q;
    assert(q.empty_approx());
    assert(!q.try_pop().has_value());
    std::printf("  test_empty_queue: PASSED\n");
}

static void test_single_thread_fifo() {
    MpscRing<uint64_t, 64> q;

    for (uint64_t i = 0; i < 32; ++i) {
        assert(q.try_push(i));
    }
    for (uint64_t i = 0; i < 32; ++i) {
        const auto opt = q.try_pop();
        assert(opt.has_value());
        assert(*opt == i);
    }
    assert(q.empty_approx());
    assert(!q.try_pop().has_value());

    std::printf("  test_single_thread_fifo: PASSED\n");
}

static void test_capacity_bound() {
    MpscRing<uint64_t, 8> q;

    for (uint64_t i = 0; i < 8; ++i) {
        assert(q.try_push(i));
    }
    assert(!q.try_push(99));

    auto first = q.try_pop();
    assert(first.has_value() && *first == 0);
    assert(q.try_push(99));
    assert(!q.try_push(100));

    for (uint64_t expected : {1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL, 99ULL}) {
        auto opt = q.try_pop();
        assert(opt.has_value());
        assert(*opt == expected);
    }
    assert(q.empty_approx());

    std::printf("  test_capacity_bound: PASSED\n");
}

// Pushing and draining repeatedly makes each cell sequence move from
// drained back to ready for the next round.

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

static void test_stress_m_producers_one_consumer() {
    constexpr std::size_t N_PRODUCERS = 4;
    constexpr std::size_t N_PER_PRODUCER = 50000;
    constexpr std::size_t CAPACITY = 1024;

    std::printf("  test_stress_m_producers_one_consumer: %zu producers × %zu items, capacity %zu...\n", N_PRODUCERS,
                N_PER_PRODUCER, CAPACITY);

    MpscRing<uint64_t, CAPACITY> q;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::vector<std::atomic<bool>>> markers(N_PRODUCERS);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v)
            m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t producer_id, uint64_t seq) -> uint64_t {
        return (static_cast<uint64_t>(producer_id) << 48) | seq;
    };
    auto decode_producer = [](uint64_t item) -> std::size_t {
        // size_t and uint64_t are the same type here, so an explicit
        // cast would be flagged useless.
        return item >> 48;
    };
    auto decode_seq = [](uint64_t item) -> uint64_t { return item & ((uint64_t{1} << 48) - 1); };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                const uint64_t item = encode(p, s);
                while (!q.try_push(item)) {
                    CRUCIBLE_SPIN_PAUSE;
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
                if (producers_done.load(std::memory_order_acquire) == N_PRODUCERS) {
                    // An item can still be in flight after its producer
                    // sets the done flag, so drain once more.
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
                        CRUCIBLE_SPIN_PAUSE;
                    }
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        }
    });

    producers.clear();
    consumer = std::jthread{};

    std::size_t missing = 0;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        for (std::size_t s = 0; s < N_PER_PRODUCER; ++s) {
            if (!markers[p][s].load(std::memory_order_relaxed)) {
                ++missing;
            }
        }
    }
    const uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n", N_PRODUCERS * N_PER_PRODUCER, missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0 && "item lost — producer pushed but consumer never received");
    assert(dup == 0 && "item duplicated — single-consumer protocol is broken");

    std::printf("  test_stress_m_producers_one_consumer: PASSED\n");
}

// Few slots and many producers, so try_push takes its CAS retry path.

static void test_stress_high_contention() {
    constexpr std::size_t N_PRODUCERS = 8;
    constexpr std::size_t N_PER_PRODUCER = 10000;
    constexpr std::size_t CAPACITY = 16;

    std::printf("  test_stress_high_contention: %zu producers × %zu items, capacity %zu...\n", N_PRODUCERS,
                N_PER_PRODUCER, CAPACITY);

    MpscRing<uint64_t, CAPACITY> q;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<uint64_t> duplicate_count{0};

    std::vector<std::vector<std::atomic<bool>>> markers(N_PRODUCERS);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v)
            m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t p, uint64_t s) { return (static_cast<uint64_t>(p) << 48) | s; };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                while (!q.try_push(encode(p, s))) {
                    CRUCIBLE_SPIN_PAUSE;
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
            } else if (producers_done.load(std::memory_order_acquire) == N_PRODUCERS && received == total) {
                break;
            } else {
                CRUCIBLE_SPIN_PAUSE;
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

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n", N_PRODUCERS * N_PER_PRODUCER, missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0 && "item lost under high producer contention");
    assert(dup == 0 && "item duplicated under high producer contention");

    std::printf("  test_stress_high_contention: PASSED\n");
}

static void test_batched_round_trip() {
    std::printf("  test_batched_round_trip: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 256>>();

    constexpr size_t N = 64;
    std::array<uint64_t, N> tx{};
    for (size_t i = 0; i < N; ++i)
        tx[i] = 1000 + i;

    const size_t pushed = ring->try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == N && "batch push must be all-or-nothing");

    std::array<uint64_t, N> rx{};
    const size_t popped = ring->try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == N);

    for (size_t i = 0; i < N; ++i) {
        assert(rx[i] == 1000 + i && "FIFO order broken");
    }
    std::printf("PASSED\n");
}

static void test_batched_wrap_around() {
    std::printf("  test_batched_wrap_around: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 16>>();

    // Twelve pushes and twelve pops leave head and tail at 12.
    for (uint64_t i = 0; i < 12; ++i)
        assert(ring->try_push(i));
    for (uint64_t i = 0; i < 12; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }

    // A batch of 8 now spans cells 12..15 then 0..3.
    std::array<uint64_t, 8> tx{};
    for (size_t i = 0; i < 8; ++i)
        tx[i] = 100 + i;
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

    for (uint64_t i = 0; i < 16; ++i)
        assert(ring->try_push(i));

    std::array<uint64_t, 4> tx{99, 99, 99, 99};
    const size_t pushed = ring->try_push_batch(std::span<const uint64_t>(tx));
    assert(pushed == 0 && "full queue must reject batch entirely");

    // No 99 appears, so the rejected batch wrote nothing.
    for (uint64_t i = 0; i < 16; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }
    std::printf("PASSED\n");
}

static void test_batched_mixed_with_singles() {
    std::printf("  test_batched_mixed_with_singles: ");
    auto ring = std::make_unique<MpscRing<uint64_t, 256>>();

    for (uint64_t i = 0; i < 10; ++i)
        assert(ring->try_push(i));

    std::array<uint64_t, 32> tx{};
    for (size_t i = 0; i < 32; ++i)
        tx[i] = 100 + i;
    assert(ring->try_push_batch(std::span<const uint64_t>(tx)) == 32);

    for (uint64_t i = 0; i < 10; ++i)
        assert(ring->try_push(200 + i));

    for (uint64_t i = 0; i < 10; ++i) {
        auto v = ring->try_pop();
        assert(v && *v == i);
    }
    std::array<uint64_t, 32> rx{};
    assert(ring->try_pop_batch(std::span<uint64_t>(rx)) == 32);
    for (size_t i = 0; i < 32; ++i)
        assert(rx[i] == 100 + i);
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
        producers.emplace_back([&ring, &start, &encode, p, BATCH](std::stop_token) {
            while (!start.load(std::memory_order_acquire)) { /* spin */
            }

            std::array<uint64_t, BATCH> buf{};
            for (size_t base = 0; base < ITEMS_PER_PRODUCER; base += BATCH) {
                const size_t n = std::min(BATCH, ITEMS_PER_PRODUCER - base);
                for (size_t i = 0; i < n; ++i) {
                    buf[i] = encode(p, base + i);
                }
                size_t pushed = 0;
                while (pushed < n) {
                    const size_t r = ring->try_push_batch(std::span<const uint64_t>(buf.data() + pushed, n - pushed));
                    if (r > 0) {
                        pushed += r;
                    } else {
                        CRUCIBLE_SPIN_PAUSE;
                    }
                }
            }
        });
    }

    std::jthread consumer([&ring, &seen, &consumed, &start](std::stop_token) {
        while (!start.load(std::memory_order_acquire)) { /* spin */
        }
        std::array<uint64_t, 32> buf{};
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            const size_t n = ring->try_pop_batch(std::span<uint64_t>(buf));
            if (n == 0) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                seen[buf[i]].fetch_add(1, std::memory_order_relaxed);
            }
            consumed.fetch_add(n, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);
    producers.clear();  // join
    consumer.join();

    int missing = 0, dup = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0)
            ++missing;
        else if (c > 1)
            dup += c - 1;
    }
    std::printf("    expected: %zu, missing: %d, duplicates: %d\n", TOTAL, missing, dup);
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

    test_batched_round_trip();
    test_batched_wrap_around();
    test_batched_full_rejection();
    test_batched_mixed_with_singles();
    test_batched_multi_producer_stress();

    std::printf("test_mpsc_ring: ALL PASSED\n");
    return 0;
}
