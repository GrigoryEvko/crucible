// The concurrent tests need no reference implementation to compare
// against, because each item carries the identity of the producer that
// sent it and its place in that producer's stream.  After the threads
// stop, every item must be marked exactly once: a missing mark means
// the grid dropped work, a repeated one means two consumers were handed
// the same work.

#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/_SpscRing.h>

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

using TestRing = SpscRing<uint64_t, 64>;
using TestGrid = ShardedSpscGrid<uint64_t, 4, 4, 256>;

static_assert(!std::is_copy_constructible_v<TestRing>);
static_assert(!std::is_move_constructible_v<TestRing>);
static_assert(!std::is_copy_constructible_v<TestGrid>);
static_assert(!std::is_move_constructible_v<TestGrid>);
static_assert(TestRing::capacity() == 64);
static_assert(TestGrid::num_producers() == 4);
static_assert(TestGrid::num_consumers() == 4);
static_assert(TestGrid::ring_capacity() == 256);

static void test_spsc_ring_basic() {
    SpscRing<uint64_t, 8> r;
    assert(r.empty_approx());
    assert(!r.try_pop().has_value());

    for (uint64_t i = 0; i < 8; ++i) {
        assert(r.try_push(i));
    }
    assert(!r.try_push(99));
    assert(r.size_approx() == 8);

    for (uint64_t i = 0; i < 8; ++i) {
        auto opt = r.try_pop();
        assert(opt.has_value());
        assert(*opt == i);
    }
    assert(r.empty_approx());

    std::printf("  test_spsc_ring_basic: PASSED\n");
}

// Ten fill-and-drain cycles on a four-slot ring take the indices well
// past the end of the buffer, so the wrap is exercised repeatedly
// rather than once.
static void test_spsc_ring_wrap() {
    SpscRing<uint64_t, 4> r;

    for (uint64_t cycle = 0; cycle < 10; ++cycle) {
        for (uint64_t i = 0; i < 4; ++i) {
            assert(r.try_push(cycle * 100 + i));
        }
        for (uint64_t i = 0; i < 4; ++i) {
            auto opt = r.try_pop();
            assert(opt.has_value());
            assert(*opt == cycle * 100 + i);
        }
    }

    std::printf("  test_spsc_ring_wrap: PASSED\n");
}

static void test_spsc_ring_threaded() {
    SpscRing<uint64_t, 1024> r;
    constexpr uint64_t N = 200000;
    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> received{0};
    std::vector<uint64_t> mirror;
    mirror.reserve(N);

    std::jthread producer([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N; ++i) {
            while (!r.try_push(i)) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::jthread consumer([&](std::stop_token /*st*/) {
        while (received.load(std::memory_order_relaxed) < N) {
            if (auto opt = r.try_pop()) {
                mirror.push_back(*opt);
                received.fetch_add(1, std::memory_order_release);
            } else if (producer_done.load(std::memory_order_acquire) && r.empty_approx()) {
                break;
            } else {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    });

    // Assigning a default-constructed thread destroys the running one,
    // which joins it.  Main must not read the mirror before this.
    producer = std::jthread{};
    consumer = std::jthread{};

    assert(mirror.size() == N);
    for (uint64_t i = 0; i < N; ++i) {
        assert(mirror[i] == i);
    }

    std::printf("  test_spsc_ring_threaded: PASSED (%llu items)\n", static_cast<unsigned long long>(N));
}

// Eight items across four consumers means two full rounds, so each
// consumer holds exactly two and their values are four apart.
static void test_grid_round_robin_single_thread() {
    ShardedSpscGrid<uint64_t, 1, 4, 16> grid;

    for (uint64_t i = 0; i < 8; ++i) {
        assert(grid.try_push(0, i));
    }

    for (std::size_t c = 0; c < 4; ++c) {
        assert(grid.size_approx(0, c) == 2);
    }

    for (std::size_t c = 0; c < 4; ++c) {
        auto first = grid.try_pop(c);
        assert(first.has_value());
        assert(*first == c);
        auto second = grid.try_pop(c);
        assert(second.has_value());
        assert(*second == c + 4);
    }

    for (std::size_t c = 0; c < 4; ++c) {
        assert(!grid.try_pop(c).has_value());
    }

    std::printf("  test_grid_round_robin_single_thread: PASSED\n");
}

// An item packs its key into the high half and its position within that
// key into the low half, so a single value identifies both.
struct KeyExtract {
    [[nodiscard]] std::uint64_t operator()(uint64_t v) const noexcept { return v >> 32; }
};

static void test_grid_hash_key_ordering() {
    ShardedSpscGrid<uint64_t, 1, 4, 64, HashKeyRouting<KeyExtract>> grid;

    // Three keys across four consumers, so at least one consumer takes
    // none and the routing cannot pass by spreading items evenly.
    constexpr uint64_t KEYS[] = {0xAAAA, 0xBBBB, 0xCCCC};
    for (uint64_t k : KEYS) {
        for (uint64_t s = 0; s < 10; ++s) {
            const uint64_t item = (k << 32) | s;
            assert(grid.try_push(0, item));
        }
    }

    // Everything is collected before anything is checked.  Popping is
    // the only way to see what a ring holds, and pushing an item back
    // would send it to the tail of its queue, destroying the very
    // ordering being examined.
    std::array<std::vector<uint64_t>, 4> received;
    for (std::size_t c = 0; c < 4; ++c) {
        while (auto opt = grid.try_pop(c)) {
            received[c].push_back(*opt);
        }
    }

    for (std::size_t c = 0; c < 4; ++c) {
        assert(!grid.try_pop(c).has_value());
    }

    std::size_t total = 0;
    for (const auto& v : received)
        total += v.size();
    assert(total == 30);

    // Two claims per key: every item for it reached one consumer and
    // not several, and within that consumer the items are still in the
    // order the producer sent them.
    for (uint64_t k : KEYS) {
        std::size_t target_consumer = static_cast<std::size_t>(-1);
        for (std::size_t c = 0; c < 4; ++c) {
            for (uint64_t item : received[c]) {
                if (KeyExtract{}(item) == k) {
                    if (target_consumer == static_cast<std::size_t>(-1)) {
                        target_consumer = c;
                    } else if (target_consumer != c) {
                        std::fprintf(stderr,
                                     "HashKeyRouting broken: key 0x%llx found on "
                                     "consumers %zu AND %zu\n",
                                     static_cast<unsigned long long>(k), target_consumer, c);
                        std::abort();
                    }
                }
            }
        }
        assert(target_consumer != static_cast<std::size_t>(-1) && "no consumer received any item for this key");

        // Items for other keys are interleaved in the same stream, so
        // the walk skips them and checks only that the positions for
        // this key rise one at a time.
        uint64_t expected_seq = 0;
        for (uint64_t item : received[target_consumer]) {
            if (KeyExtract{}(item) == k) {
                const uint64_t actual_seq = item & 0xFFFFFFFFu;
                if (actual_seq != expected_seq) {
                    std::fprintf(stderr,
                                 "Per-key ordering broken: key 0x%llx on consumer %zu "
                                 "expected seq %llu, got %llu\n",
                                 static_cast<unsigned long long>(k), target_consumer,
                                 static_cast<unsigned long long>(expected_seq),
                                 static_cast<unsigned long long>(actual_seq));
                    std::abort();
                }
                ++expected_seq;
            }
        }
        assert(expected_seq == 10 && "did not see all 10 items for key");
    }

    std::printf("  test_grid_hash_key_ordering: PASSED\n");
}

// Each consumer thread drains only its own column, so no two consumers
// ever touch the same ring and the exactly-once property is a claim
// about the routing rather than about mutual exclusion.
static void test_grid_4x4_stress() {
    constexpr std::size_t M = 4;
    constexpr std::size_t N_consumers = 4;
    constexpr std::size_t N_PER_PRODUCER = 50000;
    constexpr std::size_t CAPACITY = 256;

    std::printf("  test_grid_4x4_stress: %zu producers × %zu items, "
                "%zu consumers, capacity %zu...\n",
                M, N_PER_PRODUCER, N_consumers, CAPACITY);

    ShardedSpscGrid<uint64_t, M, N_consumers, CAPACITY> grid;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<std::uint64_t> duplicate_count{0};

    // An item packs its producer into the high bits and its position
    // into the low ones, which is what the consumer below decodes to
    // find the flag to set.
    std::vector<std::vector<std::atomic<bool>>> markers(M);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v)
            m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t p, std::uint64_t s) -> std::uint64_t {
        return (static_cast<std::uint64_t>(p) << 48) | s;
    };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < M; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (std::uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                while (!grid.try_push(p, encode(p, s))) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    std::vector<std::jthread> consumers;
    std::atomic<std::size_t> total_received{0};
    const std::size_t total_expected = M * N_PER_PRODUCER;
    for (std::size_t c = 0; c < N_consumers; ++c) {
        consumers.emplace_back([&, c](std::stop_token /*st*/) {
            while (total_received.load(std::memory_order_relaxed) < total_expected) {
                if (auto opt = grid.try_pop(c)) {
                    const std::size_t p = *opt >> 48;
                    const std::uint64_t s = *opt & ((std::uint64_t{1} << 48) - 1);
                    // A value outside the ranges was never pushed, so
                    // reading one means an uninitialised slot was
                    // handed out.  It is counted here rather than
                    // given a channel of its own.
                    if (p >= M || s >= N_PER_PRODUCER) {
                        duplicate_count.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    bool prev = markers[p][s].exchange(true, std::memory_order_relaxed);
                    if (prev) {
                        duplicate_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        total_received.fetch_add(1, std::memory_order_release);
                    }
                } else if (producers_done.load(std::memory_order_acquire) == M
                           && total_received.load(std::memory_order_acquire) == total_expected) {
                    break;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        });
    }

    // Clearing the vectors destroys the threads, which joins them.  The
    // marks are only read afterwards.
    producers.clear();
    consumers.clear();

    std::size_t missing = 0;
    for (std::size_t p = 0; p < M; ++p) {
        for (std::size_t s = 0; s < N_PER_PRODUCER; ++s) {
            if (!markers[p][s].load(std::memory_order_relaxed)) ++missing;
        }
    }
    const std::uint64_t dup = duplicate_count.load(std::memory_order_relaxed);

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n", total_expected, missing,
                static_cast<unsigned long long>(dup));

    assert(missing == 0 && "item lost — producer sent but no consumer received");
    assert(dup == 0 && "item duplicated — sharding broken");

    std::printf("  test_grid_4x4_stress: PASSED\n");
}

int main() {
    std::printf("test_sharded_grid:\n");

    test_spsc_ring_basic();
    test_spsc_ring_wrap();
    test_spsc_ring_threaded();
    test_grid_round_robin_single_thread();
    test_grid_hash_key_ordering();
    test_grid_4x4_stress();

    std::printf("test_sharded_grid: ALL PASSED\n");
    return 0;
}
