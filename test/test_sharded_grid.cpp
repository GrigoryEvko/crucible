// ═══════════════════════════════════════════════════════════════════
// test_sharded_grid — ShardedSpscGrid + SpscRing correctness.
//
// Covers:
//   1. SpscRing single-thread round-trip + capacity bound + wrap
//   2. SpscRing multi-thread SPSC stress (1 producer + 1 consumer)
//   3. ShardedSpscGrid single-thread routing for each policy
//   4. ShardedSpscGrid 4×4 multi-thread stress: each producer sends
//      a unique stream of items; consumers collect; verify every
//      item delivered exactly once via per-(producer, seq) bitmap
//   5. HashKeyRouting per-key ordering: items with the same key
//      go to the same consumer, in producer-order
//   6. Compile-time: not copyable, not movable
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/SpscRing.h>

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

// ── Compile-time checks ────────────────────────────────────────────

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

// ── SpscRing unit: empty, FIFO, capacity ──────────────────────────

static void test_spsc_ring_basic() {
    SpscRing<uint64_t, 8> r;
    assert(r.empty_approx());
    assert(!r.try_pop().has_value());

    // Fill to capacity
    for (uint64_t i = 0; i < 8; ++i) {
        assert(r.try_push(i));
    }
    assert(!r.try_push(99));  // full
    assert(r.size_approx() == 8);

    // Drain in FIFO order
    for (uint64_t i = 0; i < 8; ++i) {
        auto opt = r.try_pop();
        assert(opt.has_value());
        assert(*opt == i);
    }
    assert(r.empty_approx());

    std::printf("  test_spsc_ring_basic: PASSED\n");
}

// ── SpscRing wrap-around ──────────────────────────────────────────

static void test_spsc_ring_wrap() {
    SpscRing<uint64_t, 4> r;

    // Push/pop cycles past CAPACITY to force wrap
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

// ── SpscRing SPSC stress ──────────────────────────────────────────

static void test_spsc_ring_threaded() {
    SpscRing<uint64_t, 1024> r;
    constexpr uint64_t N = 200'000;
    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> received{0};
    std::vector<uint64_t> mirror;
    mirror.reserve(N);

    std::jthread producer([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N; ++i) {
            while (!r.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::jthread consumer([&](std::stop_token /*st*/) {
        while (received.load(std::memory_order_relaxed) < N) {
            if (auto opt = r.try_pop()) {
                mirror.push_back(*opt);
                received.fetch_add(1, std::memory_order_release);
            } else if (producer_done.load(std::memory_order_acquire)
                       && r.empty_approx()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer = std::jthread{};
    consumer = std::jthread{};

    assert(mirror.size() == N);
    for (uint64_t i = 0; i < N; ++i) {
        assert(mirror[i] == i);  // FIFO preserved
    }

    std::printf("  test_spsc_ring_threaded: PASSED (%llu items)\n",
                static_cast<unsigned long long>(N));
}

// ── ShardedSpscGrid single-thread RoundRobin routing ──────────────

static void test_grid_round_robin_single_thread() {
    ShardedSpscGrid<uint64_t, 1, 4, 16> grid;

    // Single producer sends 8 items; round-robin should distribute
    // 2 items to each of 4 consumers.
    for (uint64_t i = 0; i < 8; ++i) {
        assert(grid.try_push(0, i));
    }

    // Each consumer gets 2 items
    for (std::size_t c = 0; c < 4; ++c) {
        assert(grid.size_approx(0, c) == 2);
    }

    // Drain and verify FIFO per consumer
    for (std::size_t c = 0; c < 4; ++c) {
        auto first = grid.try_pop(c);
        assert(first.has_value());
        assert(*first == c);  // first round: producer sent to consumer c=seq%4
        auto second = grid.try_pop(c);
        assert(second.has_value());
        assert(*second == c + 4);  // second round: c=(seq+4)%4=c
    }

    // All consumers empty now
    for (std::size_t c = 0; c < 4; ++c) {
        assert(!grid.try_pop(c).has_value());
    }

    std::printf("  test_grid_round_robin_single_thread: PASSED\n");
}

// ── ShardedSpscGrid HashKeyRouting per-key ordering ───────────────

struct KeyExtract {
    [[nodiscard]] std::uint64_t operator()(uint64_t v) const noexcept {
        return v >> 32;  // high 32 bits = key
    }
};

static void test_grid_hash_key_ordering() {
    ShardedSpscGrid<uint64_t, 1, 4, 64,
                    HashKeyRouting<KeyExtract>> grid;

    // Send 10 items each for 3 distinct keys.
    // Encoding: high 32 = key, low 32 = sequence within key.
    constexpr uint64_t KEYS[] = {0xAAAA, 0xBBBB, 0xCCCC};
    for (uint64_t k : KEYS) {
        for (uint64_t s = 0; s < 10; ++s) {
            const uint64_t item = (k << 32) | s;
            assert(grid.try_push(0, item));
        }
    }

    // Drain all consumers into per-consumer vectors.  Collecting
    // first (instead of peek-and-resend) avoids perturbing the FIFO
    // order within each ring — try_recv IS the only way to observe
    // ring contents, and sending items back would re-route them to
    // the tail of their target consumer's queue, breaking the
    // ordering invariant we're trying to verify.
    std::array<std::vector<uint64_t>, 4> received;
    for (std::size_t c = 0; c < 4; ++c) {
        while (auto opt = grid.try_pop(c)) {
            received[c].push_back(*opt);
        }
    }

    // All rings should now be empty.
    for (std::size_t c = 0; c < 4; ++c) {
        assert(!grid.try_pop(c).has_value());
    }

    // Verify total items received = total sent.
    std::size_t total = 0;
    for (const auto& v : received) total += v.size();
    assert(total == 30);  // 3 keys × 10 items

    // Verify per-key invariants:
    //   (a) all items for key k landed in ONE consumer (HashKey
    //       routing is deterministic)
    //   (b) within that consumer, items for key k appear in
    //       producer-order (seq 0, 1, 2, ..., 9)
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
                            static_cast<unsigned long long>(k),
                            target_consumer, c);
                        std::abort();
                    }
                }
            }
        }
        assert(target_consumer != static_cast<std::size_t>(-1)
            && "no consumer received any item for this key");

        // Extract the per-key sub-sequence from target_consumer's
        // received stream; sequence numbers MUST be 0, 1, ..., 9
        // in order (producer-order preserved).
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

// ── ShardedSpscGrid 4×4 multi-thread stress ───────────────────────
//
// 4 producer threads each send N items; 4 consumer threads each
// drain its column.  Item-tracking invariant: every (producer,
// seq) received exactly once across all consumers.

static void test_grid_4x4_stress() {
    constexpr std::size_t M = 4;
    constexpr std::size_t N_consumers = 4;
    constexpr std::size_t N_PER_PRODUCER = 50'000;
    constexpr std::size_t CAPACITY = 256;

    std::printf("  test_grid_4x4_stress: %zu producers × %zu items, "
                "%zu consumers, capacity %zu...\n",
                M, N_PER_PRODUCER, N_consumers, CAPACITY);

    ShardedSpscGrid<uint64_t, M, N_consumers, CAPACITY> grid;
    std::atomic<std::size_t> producers_done{0};
    std::atomic<std::uint64_t> duplicate_count{0};

    // Per-(producer, seq) marker bitmap.  Item encoding:
    //   high 16 = producer id, low 48 = seq.
    std::vector<std::vector<std::atomic<bool>>> markers(M);
    for (auto& v : markers) {
        v = std::vector<std::atomic<bool>>(N_PER_PRODUCER);
        for (auto& m : v) m.store(false, std::memory_order_relaxed);
    }

    auto encode = [](std::size_t p, std::uint64_t s) -> std::uint64_t {
        return (static_cast<std::uint64_t>(p) << 48) | s;
    };

    std::vector<std::jthread> producers;
    for (std::size_t p = 0; p < M; ++p) {
        producers.emplace_back([&, p](std::stop_token /*st*/) {
            for (std::uint64_t s = 0; s < N_PER_PRODUCER; ++s) {
                while (!grid.try_push(p, encode(p, s))) {
                    std::this_thread::yield();
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
            while (total_received.load(std::memory_order_relaxed)
                   < total_expected) {
                if (auto opt = grid.try_pop(c)) {
                    const std::size_t p = *opt >> 48;
                    const std::uint64_t s = *opt & ((std::uint64_t{1} << 48) - 1);
                    if (p >= M || s >= N_PER_PRODUCER) {
                        duplicate_count.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    bool prev = markers[p][s].exchange(
                        true, std::memory_order_relaxed);
                    if (prev) {
                        duplicate_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        total_received.fetch_add(1, std::memory_order_release);
                    }
                } else if (producers_done.load(std::memory_order_acquire) == M
                           && total_received.load(std::memory_order_acquire)
                              == total_expected) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    producers.clear();
    consumers.clear();

    // Verify every (producer, seq) was received exactly once.
    std::size_t missing = 0;
    for (std::size_t p = 0; p < M; ++p) {
        for (std::size_t s = 0; s < N_PER_PRODUCER; ++s) {
            if (!markers[p][s].load(std::memory_order_relaxed)) ++missing;
        }
    }
    const std::uint64_t dup =
        duplicate_count.load(std::memory_order_relaxed);

    std::printf("    expected: %zu, missing: %zu, duplicates: %llu\n",
                total_expected, missing,
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
