// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, calls its smoke test with non-constant
// arguments, and pushes the split traits past the shard counts the header
// checks against itself.

#include <crucible/safety/PermissionTreeGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

struct MyData {};

static_assert(can_split_n_v<MyData, 1>);
static_assert(can_split_n_v<MyData, 2>);
static_assert(can_split_n_v<MyData, 4>);
static_assert(can_split_n_v<MyData, 8>);
static_assert(can_split_n_v<MyData, 16>);
static_assert(can_split_n_v<MyData, 64>);
static_assert(can_split_n_v<MyData, 128>);

// A split into zero shards has no meaning. The floor is pinned here so a
// dispatcher can rely on it rather than re-check.
static_assert(!can_split_n_v<MyData, 0>);

static_assert(std::is_same_v<auto_split_n_t<MyData, 4>,
                             std::tuple<Slice<MyData, 0>, Slice<MyData, 1>, Slice<MyData, 2>, Slice<MyData, 3>>>);

static_assert(std::is_same_v<
              auto_split_n_permissions_t<MyData, 3>,
              std::tuple<Permission<Slice<MyData, 0>>, Permission<Slice<MyData, 1>>, Permission<Slice<MyData, 2>>>>);

// A templated, payload-bearing parent. Nothing in the specialization
// machinery may require the parent to be empty or trivially constructible,
// and a tag like this one is what catches such a requirement.

template <typename U>
struct ChannelTag {
    using payload_type = U;
};

static_assert(can_split_n_v<ChannelTag<int>, 8>);
static_assert(std::is_same_v<auto_split_n_t<ChannelTag<int>, 2>,
                             std::tuple<Slice<ChannelTag<int>, 0>, Slice<ChannelTag<int>, 1>>>);

// Slices of different parents are different types, so mixing two trees is
// a compile error rather than a runtime surprise.
static_assert(!std::is_same_v<Slice<MyData, 0>, Slice<ChannelTag<int>, 0>>);

template <std::size_t... Is>
constexpr bool stress_pack_(std::index_sequence<Is...>) noexcept {
    return splits_into_pack_v<MyData, Slice<MyData, Is>...>;
}

static_assert(stress_pack_<>(std::make_index_sequence<16>{}));
static_assert(stress_pack_<>(std::make_index_sequence<32>{}));

void run_runtime_smoke() {
    runtime_smoke_test();

    // The header keeps its own smoke test at four shards to stay readable.
    // This one goes further.
    auto parent = mint_permission_root<MyData>();
    auto eight = mint_permission_split_n<Slice<MyData, 0>, Slice<MyData, 1>, Slice<MyData, 2>, Slice<MyData, 3>,
                                         Slice<MyData, 4>, Slice<MyData, 5>, Slice<MyData, 6>, Slice<MyData, 7>>(
        std::move(parent));

    static_assert(std::is_same_v<decltype(eight), auto_split_n_permissions_t<MyData, 8>>);

    // The children destruct at scope end. The linear discipline forbids
    // reuse, so there is nothing to tear down by hand.
    (void)eight;
}

}  // namespace

int main() {
    std::printf("[test_permission_tree_generator]\n");
    run_runtime_smoke();
    std::printf("  runtime_smoke: PASSED\n");
    std::printf("  static_assert checks: PASSED\n");
    return 0;
}
