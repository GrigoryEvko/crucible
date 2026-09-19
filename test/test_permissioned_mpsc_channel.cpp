#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/permissions/_Permission.h>

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

struct InboxChannel {};
struct WorkerInboxChannel {};
struct DrainedChannel {};

using IntChannel = PermissionedMpscChannel<int, 256, InboxChannel>;
using ProducerH = IntChannel::ProducerHandle;
using ConsumerH = IntChannel::ConsumerHandle;

static_assert(!std::is_copy_constructible_v<ProducerH>);
static_assert(std::is_move_constructible_v<ProducerH>);
static_assert(!std::is_copy_constructible_v<ConsumerH>);
static_assert(std::is_move_constructible_v<ConsumerH>);

// A consumer binds to one channel for life, so it cannot be assigned over.
static_assert(!std::is_move_assignable_v<ConsumerH>);

// A producer holds one share, which its destructor releases.  Assigning
// over it would leak that share.
static_assert(!std::is_move_assignable_v<ProducerH>);

// Handles come from the factory only.
static_assert(!std::is_default_constructible_v<ProducerH>);
static_assert(!std::is_default_constructible_v<ConsumerH>);

void test_single_thread_round_trip() {
    PermissionedMpscChannel<int, 64, InboxChannel> ch;

    auto cons_perm = mint_permission_root<mpsc_tag::Consumer<InboxChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());
    auto producer = std::move(*p_opt);

    for (int i = 0; i < 5; ++i) {
        CRUCIBLE_TEST_REQUIRE(producer.try_push(i));
    }

    int sum = 0;
    int popped = 0;
    while (auto v = consumer.try_pop()) {
        sum += *v;
        ++popped;
    }
    CRUCIBLE_TEST_REQUIRE(popped == 5);
    CRUCIBLE_TEST_REQUIRE(sum == 0 + 1 + 2 + 3 + 4);
}

void test_multi_producer_drain() {
    constexpr int N_PRODUCERS = 4;
    constexpr int PER_PRODUCER = 1000;
    PermissionedMpscChannel<int, 1024, WorkerInboxChannel> ch;

    auto cons_perm = mint_permission_root<mpsc_tag::Consumer<WorkerInboxChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    std::atomic<int> total_pushed{0};
    std::vector<std::jthread> producers;
    producers.reserve(N_PRODUCERS);

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

    int total_popped = 0;
    int expected = N_PRODUCERS * PER_PRODUCER;
    while (total_popped < expected) {
        if (auto v = consumer.try_pop()) {
            (void)v;
            ++total_popped;
        } else {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

    for (auto& t : producers)
        t.join();

    CRUCIBLE_TEST_REQUIRE(total_pushed.load() == expected);
    CRUCIBLE_TEST_REQUIRE(total_popped == expected);

    CRUCIBLE_TEST_REQUIRE(ch.empty_approx());
}

void test_drained_access_refuses_while_shares_out() {
    PermissionedMpscChannel<int, 32, DrainedChannel> ch;
    auto cons_perm = mint_permission_root<mpsc_tag::Consumer<DrainedChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    // Drained access must refuse while a producer share is outstanding.
    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    bool body_ran = false;
    bool ok = ch.with_drained_access([&]() noexcept { body_ran = true; });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!body_ran);

    // With the share dropped it must succeed.
    p_opt.reset();
    body_ran = false;
    ok = ch.with_drained_access([&]() noexcept { body_ran = true; });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(body_ran);

    // The channel returns to lending once the body has returned.
    auto p2 = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p2.has_value());

    CRUCIBLE_TEST_REQUIRE(p2->try_push(42));
    auto v = consumer.try_pop();
    CRUCIBLE_TEST_REQUIRE(v.has_value() && *v == 42);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_mpsc_channel]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_producer_drain", test_multi_producer_drain);
    run_test("drained_access_refuses_while_shares_out", test_drained_access_refuses_while_shares_out);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
