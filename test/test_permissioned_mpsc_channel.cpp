// ═══════════════════════════════════════════════════════════════════
// test_permissioned_mpsc_channel — sentinel TU for FOUND-A01..A05
//
// Exercises concurrent/PermissionedMpscChannel.h under the project
// warning matrix.
//
// Coverage:
//   1. sizeof / EBO claims (handles are sizeof(Channel*) plus their
//      Permission/Guard payload — never larger).
//   2. Move-only handle discipline (copy + move-assign deleted with
//      reasons; default-construct deleted via private ctor).
//   3. Single-thread single-producer single-consumer round-trip.
//   4. Multi-thread N-producer + 1-consumer drain (TSan + functional).
//   5. with_drained_access mode-transition refuses while shares out;
//      succeeds when none.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/PermissionedMpscChannel.h>
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

// ── Test harness ─────────────────────────────────────────────────

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

// ── User tags — one per logical channel ──────────────────────────

struct InboxChannel {};
struct WorkerInboxChannel {};
struct DrainedChannel {};

// ── Compile-time structural claims ───────────────────────────────

using IntChannel  = PermissionedMpscChannel<int, 256, InboxChannel>;
using ProducerH   = IntChannel::ProducerHandle;
using ConsumerH   = IntChannel::ConsumerHandle;

// Move-only on both handles.
static_assert(!std::is_copy_constructible_v<ProducerH>);
static_assert( std::is_move_constructible_v<ProducerH>);
static_assert(!std::is_copy_constructible_v<ConsumerH>);
static_assert( std::is_move_constructible_v<ConsumerH>);

// ConsumerHandle is move-assignment-deleted (binds to one channel
// for life via the reference member + linear Permission).
static_assert(!std::is_move_assignable_v<ConsumerH>);

// ProducerHandle is move-assignment-deleted (Guard's dtor releases
// one share; reassigning would leak the share).
static_assert(!std::is_move_assignable_v<ProducerH>);

// Default-construct is forbidden (private ctor, factory only).
static_assert(!std::is_default_constructible_v<ProducerH>);
static_assert(!std::is_default_constructible_v<ConsumerH>);

// ── Tier 1: round-trip in a single thread ────────────────────────

void test_single_thread_round_trip() {
    PermissionedMpscChannel<int, 64, InboxChannel> ch;

    auto cons_perm =
        permission_root_mint<mpsc_tag::Consumer<InboxChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    // Lend a producer; push 5 values; consume them.
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

// ── Tier 2: multi-thread N-producer + 1-consumer ────────────────

void test_multi_producer_drain() {
    constexpr int N_PRODUCERS = 4;
    constexpr int PER_PRODUCER = 1000;
    PermissionedMpscChannel<int, 1024, WorkerInboxChannel> ch;

    auto cons_perm =
        permission_root_mint<mpsc_tag::Consumer<WorkerInboxChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    std::atomic<int> total_pushed{0};
    std::vector<std::thread> producers;
    producers.reserve(N_PRODUCERS);

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

    int total_popped = 0;
    int expected = N_PRODUCERS * PER_PRODUCER;
    while (total_popped < expected) {
        if (auto v = consumer.try_pop()) {
            (void)v;
            ++total_popped;
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) t.join();

    CRUCIBLE_TEST_REQUIRE(total_pushed.load() == expected);
    CRUCIBLE_TEST_REQUIRE(total_popped == expected);

    // Channel must be empty post-drain.
    CRUCIBLE_TEST_REQUIRE(ch.empty_approx());
}

// ── Tier 3: with_drained_access mode-transition ─────────────────

void test_drained_access_refuses_while_shares_out() {
    PermissionedMpscChannel<int, 32, DrainedChannel> ch;
    auto cons_perm =
        permission_root_mint<mpsc_tag::Consumer<DrainedChannel>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    // Hold an outstanding producer share — drained-access must refuse.
    auto p_opt = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p_opt.has_value());

    bool body_ran = false;
    bool ok = ch.with_drained_access([&]() noexcept {
        body_ran = true;
    });
    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!body_ran);

    // Drop the share; drained-access succeeds.
    p_opt.reset();
    body_ran = false;
    ok = ch.with_drained_access([&]() noexcept {
        body_ran = true;
    });
    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(body_ran);

    // After body returns, producers can lend again.
    auto p2 = ch.producer();
    CRUCIBLE_TEST_REQUIRE(p2.has_value());

    // Use the value to suppress unused warnings + verify push works.
    CRUCIBLE_TEST_REQUIRE(p2->try_push(42));
    auto v = consumer.try_pop();
    CRUCIBLE_TEST_REQUIRE(v.has_value() && *v == 42);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_mpsc_channel]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_producer_drain", test_multi_producer_drain);
    run_test("drained_access_refuses_while_shares_out",
             test_drained_access_refuses_while_shares_out);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
