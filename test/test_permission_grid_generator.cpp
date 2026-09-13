// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, and adds grid shapes larger than the
// ones the header checks against itself.

#include <crucible/safety/PermissionGridGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

struct GridChannel {};

static_assert(can_split_grid_v<GridChannel, 1, 1>);
static_assert(can_split_grid_v<GridChannel, 1, 8>);
static_assert(can_split_grid_v<GridChannel, 8, 1>);
static_assert(can_split_grid_v<GridChannel, 4, 4>);
static_assert(can_split_grid_v<GridChannel, 16, 8>);
static_assert(can_split_grid_v<GridChannel, 64, 32>);

static_assert(!can_split_grid_v<GridChannel, 0, 1>);
static_assert(!can_split_grid_v<GridChannel, 1, 0>);
static_assert(!can_split_grid_v<GridChannel, 0, 0>);

static_assert(!std::is_same_v<Producer<GridChannel, 0>, Consumer<GridChannel, 0>>);
static_assert(!std::is_same_v<Producer<GridChannel, 3>, Producer<GridChannel, 4>>);
static_assert(!std::is_same_v<ProducerSide<GridChannel>, ConsumerSide<GridChannel>>);

template <typename T>
struct Channel {
    using payload = T;
};
static_assert(!std::is_same_v<Producer<GridChannel, 0>, Producer<Channel<int>, 0>>);
static_assert(!std::is_same_v<ProducerSide<GridChannel>, ProducerSide<Channel<int>>>);

using Grid44 = auto_split_grid<GridChannel, 4, 4>;
static_assert(Grid44::producer_count == 4);
static_assert(Grid44::consumer_count == 4);
static_assert(std::is_same_v<Grid44::whole_type, GridChannel>);
static_assert(std::is_same_v<Grid44::producer_side_type, ProducerSide<GridChannel>>);

static_assert(std::is_same_v<Grid44::producer_perms,
                             std::tuple<Permission<Producer<GridChannel, 0>>, Permission<Producer<GridChannel, 1>>,
                                        Permission<Producer<GridChannel, 2>>, Permission<Producer<GridChannel, 3>>>>);

using Grid8x3 = auto_split_grid<GridChannel, 8, 3>;
static_assert(Grid8x3::producer_count == 8);
static_assert(Grid8x3::consumer_count == 3);
static_assert(std::tuple_size_v<Grid8x3::producer_perms> == 8);
static_assert(std::tuple_size_v<Grid8x3::consumer_perms> == 3);

static_assert(!std::is_copy_constructible_v<GridPermissions<GridChannel, 2, 2>>);
static_assert(std::is_move_constructible_v<GridPermissions<GridChannel, 2, 2>>);
static_assert(std::is_nothrow_move_constructible_v<GridPermissions<GridChannel, 2, 2>>);

void run_runtime_smoke() {
    runtime_smoke_test_grid();

    // Larger than any shape the header checks against itself.
    auto parent = mint_permission_root<GridChannel>();
    auto big_grid = mint_grid_permissions<GridChannel, 16, 8>(std::move(parent));

    static_assert(std::tuple_size_v<decltype(big_grid.producers)> == 16);
    static_assert(std::tuple_size_v<decltype(big_grid.consumers)> == 8);
    static_assert(std::is_same_v<decltype(big_grid), GridPermissions<GridChannel, 16, 8>>);

    // Many producers into one consumer: the scatter shape.
    auto p2 = mint_permission_root<GridChannel>();
    auto scatter = mint_grid_permissions<GridChannel, 32, 1>(std::move(p2));
    static_assert(std::tuple_size_v<decltype(scatter.producers)> == 32);
    static_assert(std::tuple_size_v<decltype(scatter.consumers)> == 1);

    // One producer into many consumers: the broadcast shape.
    auto p3 = mint_permission_root<GridChannel>();
    auto broadcast = mint_grid_permissions<GridChannel, 1, 32>(std::move(p3));
    static_assert(std::tuple_size_v<decltype(broadcast.producers)> == 1);
    static_assert(std::tuple_size_v<decltype(broadcast.consumers)> == 32);

    // Everything destructs at scope exit. The linear discipline is the only
    // guarantee needed, so there is nothing to tear down by hand.
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
