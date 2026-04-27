// ═══════════════════════════════════════════════════════════════════
// test_vyukov_mpmc_ring — correctness sentinel for VyukovMpmcRing
//
// The Vyukov primitive exists for head-to-head benchmark vs SCQ;
// correctness is non-negotiable regardless of throughput position.
// This sentinel exercises:
//   1. Single-thread round-trip (push N, pop N, FIFO order).
//   2. Multi-producer × multi-consumer drain (every push observed
//      exactly once; no duplicates, no losses).
//   3. Empty/full edge cases.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/VyukovMpmcRing.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using namespace crucible::concurrent;

struct TestFailure {};
#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
    } while (0)

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

void test_single_thread_round_trip() {
    VyukovMpmcRing<std::uint64_t, 16> ring;

    for (std::uint64_t i = 0; i < 10; ++i) {
        CRUCIBLE_TEST_REQUIRE(ring.try_push(i));
    }

    // FIFO order — pop should observe 0, 1, ..., 9.
    for (std::uint64_t expected = 0; expected < 10; ++expected) {
        auto v = ring.try_pop();
        CRUCIBLE_TEST_REQUIRE(v.has_value());
        CRUCIBLE_TEST_REQUIRE(*v == expected);
    }

    CRUCIBLE_TEST_REQUIRE(!ring.try_pop().has_value());
}

void test_full_then_drain() {
    constexpr std::size_t CAP = 8;
    VyukovMpmcRing<std::uint64_t, CAP> ring;

    // Push CAP items — last push must succeed; CAP+1 must fail.
    for (std::uint64_t i = 0; i < CAP; ++i) {
        CRUCIBLE_TEST_REQUIRE(ring.try_push(i));
    }
    CRUCIBLE_TEST_REQUIRE(!ring.try_push(999ULL));    // full

    // Drain exactly CAP items in FIFO order.
    int popped = 0;
    while (auto v = ring.try_pop()) {
        CRUCIBLE_TEST_REQUIRE(*v == static_cast<std::uint64_t>(popped));
        ++popped;
    }
    CRUCIBLE_TEST_REQUIRE(popped == CAP);
}

void test_multi_thread_drain() {
    constexpr int N_PRODUCERS = 4;
    constexpr int N_CONSUMERS = 4;
    constexpr int PER_PRODUCER = 5000;
    constexpr int EXPECTED = N_PRODUCERS * PER_PRODUCER;
    constexpr std::size_t CAP = 1024;

    VyukovMpmcRing<std::uint64_t, CAP> ring;

    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> producers_done{false};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Track every popped value to detect duplicates / losses.
    std::vector<std::atomic<int>> seen(EXPECTED);

    for (int t = 0; t < N_PRODUCERS; ++t) {
        producers.emplace_back([t, &ring, &total_pushed] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                std::uint64_t payload = static_cast<std::uint64_t>(
                    t * PER_PRODUCER + i);
                while (!ring.try_push(payload)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int j = 0; j < N_CONSUMERS; ++j) {
        consumers.emplace_back(
            [&ring, &total_popped, &producers_done, &seen, EXPECTED] {
                for (;;) {
                    if (auto v = ring.try_pop()) {
                        const std::uint64_t payload = *v;
                        CRUCIBLE_TEST_REQUIRE(payload <
                            static_cast<std::uint64_t>(EXPECTED));
                        seen[payload].fetch_add(1, std::memory_order_relaxed);
                        total_popped.fetch_add(1, std::memory_order_relaxed);
                    } else if (producers_done.load(std::memory_order_acquire)
                               && total_popped.load() >= EXPECTED) {
                        return;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
    }

    for (auto& t : producers) t.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& t : consumers) t.join();

    CRUCIBLE_TEST_REQUIRE(total_pushed.load() == EXPECTED);
    CRUCIBLE_TEST_REQUIRE(total_popped.load() == EXPECTED);

    // Every value must have been observed exactly once.
    int duplicates = 0;
    int losses = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(EXPECTED); ++i) {
        const int count = seen[i].load(std::memory_order_relaxed);
        if (count == 0) ++losses;
        else if (count > 1) duplicates += count - 1;
    }
    std::fprintf(stderr, "[multi-thread] losses=%d duplicates=%d ",
                 losses, duplicates);
    CRUCIBLE_TEST_REQUIRE(losses == 0);
    CRUCIBLE_TEST_REQUIRE(duplicates == 0);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_vyukov_mpmc_ring]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("full_then_drain",          test_full_then_drain);
    run_test("multi_thread_drain",       test_multi_thread_drain);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
