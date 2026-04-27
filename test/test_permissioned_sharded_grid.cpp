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
    auto whole = permission_root_mint<grid_tag::Whole<UserTag>>();
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
        if (auto v = c0.try_recv()) { sum += *v; ++total; }
        if (auto v = c1.try_recv()) { sum += *v; ++total; }
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
            if (auto v = handle.try_recv()) {
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

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_sharded_grid]\n");
    run_test("single_thread_round_trip", test_single_thread_round_trip);
    run_test("multi_thread_drain", test_multi_thread_drain);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
