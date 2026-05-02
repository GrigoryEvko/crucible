// ═══════════════════════════════════════════════════════════════════
// test_permission_grid_generator — sentinel TU for FOUND-A22
//
// Exercises safety/PermissionGridGenerator.h under the project
// warning matrix.  The header ships embedded static_asserts but
// they only fire in a TU that includes the header — this file IS
// that TU.
//
// Coverage:
//   1. Compile-time identity of Producer<Whole, I> vs Consumer<Whole, J>
//   2. can_split_grid_v at multiple (M, N) combinations
//   3. auto_split_grid type descriptor surface
//   4. Runtime smoke — mint → split_grid → destruction
//   5. Stress at non-trivial M × N (16 × 8) beyond in-header sentinels
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/PermissionGridGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

// ── User-defined Whole tag — the canonical consumer pattern ────────

struct GridChannel {};

// ── Capability check at multiple (M, N) ────────────────────────────

static_assert(can_split_grid_v<GridChannel, 1, 1>);
static_assert(can_split_grid_v<GridChannel, 1, 8>);
static_assert(can_split_grid_v<GridChannel, 8, 1>);
static_assert(can_split_grid_v<GridChannel, 4, 4>);
static_assert(can_split_grid_v<GridChannel, 16, 8>);
static_assert(can_split_grid_v<GridChannel, 64, 32>);

// Zero on either dimension is hard-rejected.
static_assert(!can_split_grid_v<GridChannel, 0, 1>);
static_assert(!can_split_grid_v<GridChannel, 1, 0>);
static_assert(!can_split_grid_v<GridChannel, 0, 0>);

// ── Type-level role distinction ────────────────────────────────────

static_assert(!std::is_same_v<
    Producer<GridChannel, 0>, Consumer<GridChannel, 0>>);
static_assert(!std::is_same_v<
    Producer<GridChannel, 3>, Producer<GridChannel, 4>>);
static_assert(!std::is_same_v<
    ProducerSide<GridChannel>, ConsumerSide<GridChannel>>);

// Distinct Whole tags produce non-interchangeable grids.
template <typename T> struct Channel { using payload = T; };
static_assert(!std::is_same_v<
    Producer<GridChannel, 0>, Producer<Channel<int>, 0>>);
static_assert(!std::is_same_v<
    ProducerSide<GridChannel>, ProducerSide<Channel<int>>>);

// ── auto_split_grid type descriptor surface ────────────────────────

using Grid44 = auto_split_grid<GridChannel, 4, 4>;
static_assert(Grid44::producer_count == 4);
static_assert(Grid44::consumer_count == 4);
static_assert(std::is_same_v<Grid44::whole_type, GridChannel>);
static_assert(std::is_same_v<Grid44::producer_side_type,
                             ProducerSide<GridChannel>>);

static_assert(std::is_same_v<
    Grid44::producer_perms,
    std::tuple<Permission<Producer<GridChannel, 0>>,
               Permission<Producer<GridChannel, 1>>,
               Permission<Producer<GridChannel, 2>>,
               Permission<Producer<GridChannel, 3>>>>);

// Asymmetric M × N — different counts on each side.
using Grid8x3 = auto_split_grid<GridChannel, 8, 3>;
static_assert(Grid8x3::producer_count == 8);
static_assert(Grid8x3::consumer_count == 3);
static_assert(std::tuple_size_v<Grid8x3::producer_perms> == 8);
static_assert(std::tuple_size_v<Grid8x3::consumer_perms> == 3);

// ── GridPermissions move-only discipline ───────────────────────────

static_assert(!std::is_copy_constructible_v<GridPermissions<GridChannel, 2, 2>>);
static_assert(std::is_move_constructible_v<GridPermissions<GridChannel, 2, 2>>);
static_assert(std::is_nothrow_move_constructible_v<
    GridPermissions<GridChannel, 2, 2>>);

// ── Runtime smoke — exercises the in-header smoke + larger grid ────

void run_runtime_smoke() {
    runtime_smoke_test_grid();

    // Stress at 16 × 8 (beyond the in-header sentinel range).
    auto parent = mint_permission_root<GridChannel>();
    auto big_grid = split_grid<GridChannel, 16, 8>(std::move(parent));

    static_assert(std::tuple_size_v<decltype(big_grid.producers)> == 16);
    static_assert(std::tuple_size_v<decltype(big_grid.consumers)> == 8);
    static_assert(std::is_same_v<
        decltype(big_grid),
        GridPermissions<GridChannel, 16, 8>>);

    // Asymmetric — many producers, few consumers (scatter pattern).
    auto p2 = mint_permission_root<GridChannel>();
    auto scatter = split_grid<GridChannel, 32, 1>(std::move(p2));
    static_assert(std::tuple_size_v<decltype(scatter.producers)> == 32);
    static_assert(std::tuple_size_v<decltype(scatter.consumers)> == 1);

    // Asymmetric — one producer, many consumers (broadcast pattern).
    auto p3 = mint_permission_root<GridChannel>();
    auto broadcast = split_grid<GridChannel, 1, 32>(std::move(p3));
    static_assert(std::tuple_size_v<decltype(broadcast.producers)> == 1);
    static_assert(std::tuple_size_v<decltype(broadcast.consumers)> == 32);

    // All destruct at scope exit — the linear discipline is the
    // only correctness guarantee needed.
    (void)big_grid;
    (void)scatter;
    (void)broadcast;
}

}  // namespace

int main() {
    std::printf("[test_permission_grid_generator]\n");
    run_runtime_smoke();
    std::printf("  runtime_smoke (4x3, 16x8, 32x1, 1x32): PASSED\n");
    std::printf("  static_assert checks: PASSED\n");
    return 0;
}
