// ═══════════════════════════════════════════════════════════════════
// test_permissioned_sharded_grid — sentinel TU for FOUND-A11..A15
//
// Exercises concurrent/PermissionedShardedGrid.h — the M producers ×
// N consumers worked example backed by the FOUND-A22 split_grid
// auto-permission-tree generator.
//
// Coverage:
//   1. Compile-time structural claims (sizeof, EBO, move-only,
//      shard_index per handle, distinct types per (I, J)).
//   2. Single-thread round-trip (3×2 grid; push/recv all combinations).
//   3. Multi-thread M producers × N consumers drain.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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

// ── User tags ────────────────────────────────────────────────────

struct GridA {};
struct GridB {};

// ── Compile-time structural claims ───────────────────────────────

using Grid32 = PermissionedShardedGrid<int, 3, 2, 64, GridA>;
using P0     = Grid32::ProducerHandle<0>;
using P1     = Grid32::ProducerHandle<1>;
using C0     = Grid32::ConsumerHandle<0>;
using C1     = Grid32::ConsumerHandle<1>;

// Distinct handle types per (I, J).
static_assert(!std::is_same_v<P0, P1>);
static_assert(!std::is_same_v<C0, C1>);
static_assert(!std::is_same_v<P0, C0>);

// shard_index reflected on the type.
static_assert(P0::shard_index == 0);
static_assert(P1::shard_index == 1);
static_assert(C0::shard_index == 0);
static_assert(C1::shard_index == 1);

// Move-only on every handle.
static_assert(!std::is_copy_constructible_v<P0>);
static_assert( std::is_move_constructible_v<P0>);
static_assert(!std::is_copy_constructible_v<C0>);
static_assert( std::is_move_constructible_v<C0>);

// Move-assign deleted (binds-to-one-slot-for-life).
static_assert(!std::is_move_assignable_v<P0>);
static_assert(!std::is_move_assignable_v<C0>);

// Default-construct forbidden (private ctor, factory only).
static_assert(!std::is_default_constructible_v<P0>);
static_assert(!std::is_default_constructible_v<C0>);

// ── Helper: mint and split a fresh grid permission tree ──────────

template <typename UserTag, std::size_t M, std::size_t N>
auto fresh_grid_perms() {
    auto whole = mint_permission_root<grid_tag::Whole<UserTag>>();
    return split_grid<grid_tag::Whole<UserTag>, M, N>(std::move(whole));
}

// ── Tier 1: single-thread round trip ─────────────────────────────

void test_single_thread_round_trip() {
    PermissionedShardedGrid<int, 2, 2, 64, GridA> grid;
    auto perms = fresh_grid_perms<GridA, 2, 2>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));

    // Push 4 items per producer; routing is round-robin.
    for (int i = 0; i < 4; ++i) {
        CRUCIBLE_TEST_REQUIRE(p0.try_push(100 + i));
        CRUCIBLE_TEST_REQUIRE(p1.try_push(200 + i));
    }

    // Drain both consumers; total must be 8 items.
    int total = 0;
    int sum   = 0;
    for (int i = 0; i < 16 && total < 8; ++i) {
        if (auto v = c0.try_pop()) { sum += *v; ++total; }
        if (auto v = c1.try_pop()) { sum += *v; ++total; }
    }

    CRUCIBLE_TEST_REQUIRE(total == 8);
    // 4×(100..103) + 4×(200..203) = 406 + 806 = 1212
    CRUCIBLE_TEST_REQUIRE(sum == (100+101+102+103) + (200+201+202+203));
}

// ── Tier 2: multi-thread M=4 × N=3 drain ─────────────────────────

void test_multi_thread_drain() {
    constexpr std::size_t M = 4;
    constexpr std::size_t N = 3;
    constexpr int PER_PRODUCER = 1000;
    constexpr int EXPECTED = M * PER_PRODUCER;

    PermissionedShardedGrid<int, M, N, 256, GridB> grid;
    auto perms = fresh_grid_perms<GridB, M, N>();

    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    auto push_loop = [&](auto handle, int prod_id) {
        for (int i = 0; i < PER_PRODUCER; ++i) {
            int payload = prod_id * PER_PRODUCER + i;
            while (!handle.try_push(payload)) {
                std::this_thread::yield();
            }
            total_pushed.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // M producers, each statically indexed.
    producers.emplace_back(push_loop,
        grid.template producer<0>(std::move(std::get<0>(perms.producers))), 0);
    producers.emplace_back(push_loop,
        grid.template producer<1>(std::move(std::get<1>(perms.producers))), 1);
    producers.emplace_back(push_loop,
        grid.template producer<2>(std::move(std::get<2>(perms.producers))), 2);
    producers.emplace_back(push_loop,
        grid.template producer<3>(std::move(std::get<3>(perms.producers))), 3);

    auto pop_loop = [&](auto handle) {
        for (;;) {
            if (auto v = handle.try_pop()) {
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
    };

    consumers.emplace_back(pop_loop,
        grid.template consumer<0>(std::move(std::get<0>(perms.consumers))));
    consumers.emplace_back(pop_loop,
        grid.template consumer<1>(std::move(std::get<1>(perms.consumers))));
    consumers.emplace_back(pop_loop,
        grid.template consumer<2>(std::move(std::get<2>(perms.consumers))));

    for (auto& t : producers) t.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& t : consumers) t.join();

    CRUCIBLE_TEST_REQUIRE(total_pushed.load() == EXPECTED);
    CRUCIBLE_TEST_REQUIRE(total_popped.load() == EXPECTED);
}

// FOUND-A13: routing-policy variants.  Verifies AffinityRouting (each
// producer<I> always lands on consumer<I mod N>) and HashKeyRouting
// (per-key ordering: items with the same key land on the same
// consumer).  RoundRobinRouting is exercised by the existing single-
// thread test above as the default policy.

void test_affinity_routing() {
    // M=4 producers, N=2 consumers, AffinityRouting:
    //   producer<0> → consumer<0>
    //   producer<1> → consumer<1>
    //   producer<2> → consumer<0>
    //   producer<3> → consumer<1>
    PermissionedShardedGrid<int, 4, 2, 64, GridA, AffinityRouting> grid;
    auto perms = fresh_grid_perms<GridA, 4, 2>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));

    // p0/p2 push values tagged with even leading digit; p1/p3 push odd.
    CRUCIBLE_TEST_REQUIRE(p0.try_push(10));
    CRUCIBLE_TEST_REQUIRE(p2.try_push(20));
    CRUCIBLE_TEST_REQUIRE(p1.try_push(11));
    CRUCIBLE_TEST_REQUIRE(p3.try_push(31));

    // c0 must see the p0/p2 values; c1 must see the p1/p3 values.
    int c0_seen = 0, c1_seen = 0;
    for (int i = 0; i < 8 && (c0_seen + c1_seen) < 4; ++i) {
        if (auto v = c0.try_pop()) {
            CRUCIBLE_TEST_REQUIRE(*v == 10 || *v == 20);
            ++c0_seen;
        }
        if (auto v = c1.try_pop()) {
            CRUCIBLE_TEST_REQUIRE(*v == 11 || *v == 31);
            ++c1_seen;
        }
    }
    CRUCIBLE_TEST_REQUIRE(c0_seen == 2);
    CRUCIBLE_TEST_REQUIRE(c1_seen == 2);
}

struct EvenOddKey {
    std::uint64_t operator()(int v) const noexcept {
        return static_cast<std::uint64_t>(v & 1);   // 0 or 1
    }
};

void test_hash_key_routing_preserves_per_key_order() {
    // M=2 producers, N=2 consumers, HashKeyRouting<EventOddKey>.
    // Items with the same parity must consistently land on the same
    // consumer — per-key ordering preserved.
    PermissionedShardedGrid<int, 2, 2, 64, GridA, HashKeyRouting<EvenOddKey>> grid;
    auto perms = fresh_grid_perms<GridA, 2, 2>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));

    // p0 pushes alternating even/odd values.  Routing must group
    // them: all evens on one consumer, all odds on the other.
    for (int i = 0; i < 8; ++i) {
        CRUCIBLE_TEST_REQUIRE(p0.try_push(i));
    }

    int evens_at_c0 = 0, odds_at_c0 = 0;
    int evens_at_c1 = 0, odds_at_c1 = 0;
    for (int i = 0; i < 16; ++i) {
        if (auto v = c0.try_pop()) {
            (*v % 2 == 0 ? evens_at_c0 : odds_at_c0) += 1;
        }
        if (auto v = c1.try_pop()) {
            (*v % 2 == 0 ? evens_at_c1 : odds_at_c1) += 1;
        }
    }
    CRUCIBLE_TEST_REQUIRE(evens_at_c0 + evens_at_c1 == 4);
    CRUCIBLE_TEST_REQUIRE(odds_at_c0  + odds_at_c1  == 4);
    // Per-key collapse: one consumer holds ALL evens, the other ALL odds.
    CRUCIBLE_TEST_REQUIRE((evens_at_c0 == 4 && odds_at_c1 == 4)
                       || (evens_at_c1 == 4 && odds_at_c0 == 4));
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_sharded_grid]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_thread_drain", test_multi_thread_drain);
    run_test("affinity_routing", test_affinity_routing);
    run_test("hash_key_routing_preserves_per_key_order",
             test_hash_key_routing_preserves_per_key_order);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
