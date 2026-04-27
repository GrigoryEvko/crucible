// ═══════════════════════════════════════════════════════════════════
// test_permissioned_mpmc_channel — sentinel TU for FOUND-A06..A10
//
// Coverage:
//   1. Compile-time sizeof / move-only / role-discriminated handles.
//   2. Single-thread round-trip.
//   3. Multi-thread N-producer × M-consumer drain (TSan + functional).
//   4. with_drained_access — refuses while EITHER pool has shares;
//      succeeds when both empty; correctly rolls back producer when
//      consumer upgrade fails.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

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

// User tags
struct WorkChannel {};
struct DrainChannel {};
struct RollbackChannel {};

// Compile-time structural claims
using IntChannel = PermissionedMpmcChannel<int, 1024, WorkChannel>;
using ProducerH  = IntChannel::ProducerHandle;
using ConsumerH  = IntChannel::ConsumerHandle;

static_assert(!std::is_copy_constructible_v<ProducerH>);
static_assert( std::is_move_constructible_v<ProducerH>);
static_assert(!std::is_copy_constructible_v<ConsumerH>);
static_assert( std::is_move_constructible_v<ConsumerH>);
static_assert(!std::is_default_constructible_v<ProducerH>);
static_assert(!std::is_default_constructible_v<ConsumerH>);

// ── Tier 1: round-trip ──────────────────────────────────────────

void test_single_thread_round_trip() {
    PermissionedMpmcChannel<int, 64, WorkChannel> ch;

    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());
    CRUCIBLE_TEST_REQUIRE(c_opt.has_value());

    auto producer = std::move(*p_opt);
    auto consumer = std::move(*c_opt);

    for (int i = 0; i < 5; ++i) {
        CRUCIBLE_TEST_REQUIRE(producer.try_push(i));
    }

    int sum = 0, popped = 0;
    while (auto v = consumer.try_pop()) {
        sum += *v;
        ++popped;
    }
    CRUCIBLE_TEST_REQUIRE(popped == 5);
    CRUCIBLE_TEST_REQUIRE(sum == 0 + 1 + 2 + 3 + 4);
}

// ── Tier 2: many-producer × many-consumer drain ────────────────

void test_multi_producer_multi_consumer_drain() {
    constexpr int N_PRODUCERS = 4;
    constexpr int N_CONSUMERS = 3;
    constexpr int PER_PRODUCER = 1000;
    constexpr int EXPECTED = N_PRODUCERS * PER_PRODUCER;

    PermissionedMpmcChannel<int, 1024, WorkChannel> ch;

    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    producers.reserve(N_PRODUCERS);
    consumers.reserve(N_CONSUMERS);

    for (int t = 0; t < N_PRODUCERS; ++t) {
        producers.emplace_back([&ch, &total_pushed, t] {
            auto p_opt = ch.producer();
            if (!p_opt) return;
            auto p = std::move(*p_opt);
            for (int i = 0; i < PER_PRODUCER; ++i) {
                while (!p.try_push(t * PER_PRODUCER + i)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::atomic<bool> producers_done{false};
    for (int j = 0; j < N_CONSUMERS; ++j) {
        consumers.emplace_back([&ch, &total_popped, &producers_done] {
            auto c_opt = ch.consumer();
            if (!c_opt) return;
            auto c = std::move(*c_opt);
            for (;;) {
                if (auto v = c.try_pop()) {
                    (void)v;
                    total_popped.fetch_add(1, std::memory_order_relaxed);
                } else {
                    if (producers_done.load(std::memory_order_acquire)
                        && total_popped.load() >= EXPECTED) {
                        return;
                    }
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
    // empty_approx() on MpmcRing's SCQ is not exact post-drain
    // (head/tail are monotonic counters, drift past the active
    // index range).  total_popped == EXPECTED is the correctness
    // invariant we care about.
}

// ── Tier 3: with_drained_access dual-pool transitions ──────────

void test_drained_refuses_with_producer_out() {
    PermissionedMpmcChannel<int, 32, DrainChannel> ch;

    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    // Producer share out — drained-access must refuse.
    bool ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    // After dropping producer, drained-access succeeds.
    p_opt.reset();
    ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
}

void test_drained_refuses_with_consumer_out_and_rolls_back() {
    PermissionedMpmcChannel<int, 32, RollbackChannel> ch;

    // Hold ONLY a consumer — producer pool empty, consumer pool has 1.
    auto c_opt = ch.consumer();
    CRUCIBLE_TEST_REQUIRE(c_opt.has_value());

    // Drained-access acquires producer-side upgrade first, then tries
    // consumer-side and FAILS, then ROLLS BACK the producer upgrade.
    // Test: after the call, a fresh producer() must succeed (proving
    // the producer pool was correctly deposited back).
    bool ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    // Producer pool must be back in normal state after rollback.
    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    // After dropping both shares, drained-access succeeds.
    c_opt.reset();
    p_opt.reset();
    ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_mpmc_channel]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_producer_multi_consumer_drain",
             test_multi_producer_multi_consumer_drain);
    run_test("drained_refuses_with_producer_out",
             test_drained_refuses_with_producer_out);
    run_test("drained_refuses_with_consumer_out_and_rolls_back",
             test_drained_refuses_with_consumer_out_and_rolls_back);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
