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

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
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

struct WorkChannel {};
struct DrainChannel {};
struct RollbackChannel {};

using IntChannel = PermissionedMpmcChannel<int, 1024, WorkChannel>;
using ProducerH = IntChannel::ProducerHandle;
using ConsumerH = IntChannel::ConsumerHandle;

static_assert(!std::is_copy_constructible_v<ProducerH>);
static_assert(std::is_move_constructible_v<ProducerH>);
static_assert(!std::is_copy_constructible_v<ConsumerH>);
static_assert(std::is_move_constructible_v<ConsumerH>);
static_assert(!std::is_default_constructible_v<ProducerH>);
static_assert(!std::is_default_constructible_v<ConsumerH>);

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

void test_multi_producer_multi_consumer_drain() {
    constexpr int N_PRODUCERS = 4;
    constexpr int N_CONSUMERS = 3;
    constexpr int PER_PRODUCER = 1000;
    constexpr int EXPECTED = N_PRODUCERS * PER_PRODUCER;

    PermissionedMpmcChannel<int, 1024, WorkChannel> ch;

    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};

    std::vector<std::jthread> producers;
    std::vector<std::jthread> consumers;
    producers.reserve(N_PRODUCERS);
    consumers.reserve(N_CONSUMERS);

    for (int t = 0; t < N_PRODUCERS; ++t) {
        producers.emplace_back([&ch, &total_pushed, t] {
            auto p_opt = ch.producer();
            if (!p_opt) return;
            auto p = std::move(*p_opt);
            for (int i = 0; i < PER_PRODUCER; ++i) {
                while (!p.try_push(t * PER_PRODUCER + i)) {
                    CRUCIBLE_SPIN_PAUSE;
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
                    if (producers_done.load(std::memory_order_acquire) && total_popped.load() >= EXPECTED) {
                        return;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        });
    }

    for (auto& t : producers)
        t.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& t : consumers)
        t.join();

    CRUCIBLE_TEST_REQUIRE(total_pushed.load() == EXPECTED);
    CRUCIBLE_TEST_REQUIRE(total_popped.load() == EXPECTED);
    // No emptiness check follows. The ring's head and tail are monotonic
    // counters that run past the active index range, so an approximate
    // emptiness query is not exact after a drain. The popped count is the
    // invariant that matters.
}

void test_drained_refuses_with_producer_out() {
    PermissionedMpmcChannel<int, 32, DrainChannel> ch;

    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    bool ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    p_opt.reset();
    ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
}

void test_drained_refuses_with_consumer_out_and_rolls_back() {
    PermissionedMpmcChannel<int, 32, RollbackChannel> ch;

    // Only a consumer is held, so the producer pool is empty and the consumer
    // pool has one share out.
    auto c_opt = ch.consumer();
    CRUCIBLE_TEST_REQUIRE(c_opt.has_value());

    // The upgrade is attempted producer-side first and succeeds, then
    // consumer-side and fails, at which point the producer upgrade is rolled
    // back. The fresh producer() below is the real assertion: it only succeeds
    // if that pool was deposited back.
    bool ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    c_opt.reset();
    p_opt.reset();
    ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
}

// Closing a handle returns one whose type carries no try_push, try_pop or
// close, so operating past the end of the protocol is a compile error rather
// than a runtime check. The closed handle still owns its pool share until it
// is destroyed, which is the runtime half of the claim.
template <typename H>
concept HasTryPush = requires(H& h, int v) { h.try_push(v); };
template <typename H>
concept HasTryPop = requires(H& h) { (void)h.try_pop(); };
template <typename H>
concept HasClose = requires(H&& h) { std::move(h).close(); };

void test_session_active_to_closed_transition() {
    using Ch = IntChannel;
    Ch ch;

    using PA = Ch::ProducerHandleT<mpmc_session::Active>;
    using PC = Ch::ProducerHandleT<mpmc_session::Closed>;
    using CA = Ch::ConsumerHandleT<mpmc_session::Active>;
    using CC = Ch::ConsumerHandleT<mpmc_session::Closed>;

    static_assert(HasTryPush<PA>, "Active producer must allow try_push");
    static_assert(HasClose<PA>, "Active producer must allow close");
    static_assert(!HasTryPush<PC>, "a closed producer must not expose try_push");
    static_assert(!HasClose<PC>, "a closed producer must not expose a second close");

    static_assert(HasTryPop<CA>, "Active consumer must allow try_pop");
    static_assert(HasClose<CA>, "Active consumer must allow close");
    static_assert(!HasTryPop<CC>, "a closed consumer must not expose try_pop");
    static_assert(!HasClose<CC>, "a closed consumer must not expose a second close");

    // The unsuffixed alias names the active state.
    static_assert(std::is_same_v<Ch::ProducerHandle, PA>);
    static_assert(std::is_same_v<Ch::ConsumerHandle, CA>);

    auto p = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p.has_value());
    CRUCIBLE_TEST_REQUIRE(p->try_push(42));

    auto closed = std::move(*p).close();
    // The active handle is already consumed, so this reset does nothing.
    p.reset();

    // The closed handle is still alive here, so its share is still out.
    bool ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!ran);

    {
        auto closed_consumer_path = std::move(closed);
        (void)closed_consumer_path;
    }
    ok = ch.with_drained_access([&]() noexcept { ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(ran);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_mpmc_channel]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_producer_multi_consumer_drain", test_multi_producer_multi_consumer_drain);
    run_test("drained_refuses_with_producer_out", test_drained_refuses_with_producer_out);
    run_test("drained_refuses_with_consumer_out_and_rolls_back", test_drained_refuses_with_consumer_out_and_rolls_back);
    run_test("session_active_to_closed_transition", test_session_active_to_closed_transition);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
