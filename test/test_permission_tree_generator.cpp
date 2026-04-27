// ═══════════════════════════════════════════════════════════════════
// test_permission_tree_generator — sentinel TU for FOUND-A21
//
// Per project memory ("header-only static_assert blind spot"):
// safety/PermissionTreeGenerator.h ships embedded static_asserts but
// won't be exercised under the project's full warning matrix unless
// at least one .cpp TU includes it.  This file IS that TU.
//
// It does three things:
//   1. Forces the header to compile under the project's warning
//      flags (catches latent template / contract / consteval bugs
//      that only surface when the code is instantiated, not when
//      it's parsed).
//   2. Calls the inline runtime_smoke_test() entry point with
//      non-constant arguments — exercises the actual factory chain
//      (permission_root_mint → permission_split_n → destruction)
//      which static_asserts cannot.
//   3. Exercises the auto_split_n traits at non-trivial N values
//      that the in-header sentinel asserts skip (16, 64, larger
//      packs that stress the index_sequence path).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/PermissionTreeGenerator.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;

// ── A user-defined parent tag — the canonical consumer pattern ─────

struct MyData {};

// ── 1D split capability check at multiple N ────────────────────────

static_assert(can_split_n_v<MyData, 1>);
static_assert(can_split_n_v<MyData, 2>);
static_assert(can_split_n_v<MyData, 4>);
static_assert(can_split_n_v<MyData, 8>);
static_assert(can_split_n_v<MyData, 16>);
static_assert(can_split_n_v<MyData, 64>);
static_assert(can_split_n_v<MyData, 128>);

// can_split_n_v<Parent, 0> must be false — splitting into zero shards
// has no operational meaning.  Verified explicitly so the dispatcher
// can rely on it as a hard floor.
static_assert(!can_split_n_v<MyData, 0>);

// auto_split_n_t produces the exact tuple of distinct Slice<> tags.
static_assert(std::is_same_v<
    auto_split_n_t<MyData, 4>,
    std::tuple<Slice<MyData, 0>, Slice<MyData, 1>,
               Slice<MyData, 2>, Slice<MyData, 3>>>);

static_assert(std::is_same_v<
    auto_split_n_permissions_t<MyData, 3>,
    std::tuple<Permission<Slice<MyData, 0>>,
               Permission<Slice<MyData, 1>>,
               Permission<Slice<MyData, 2>>>>);

// The trait composes with non-trivial parents (templated, with
// payload-bearing tags) — exercises that nothing in the
// specialization machinery accidentally requires Parent to be empty
// or trivially-default-constructible.

template <typename U> struct ChannelTag { using payload_type = U; };

static_assert(can_split_n_v<ChannelTag<int>, 8>);
static_assert(std::is_same_v<
    auto_split_n_t<ChannelTag<int>, 2>,
    std::tuple<Slice<ChannelTag<int>, 0>,
               Slice<ChannelTag<int>, 1>>>);

// Distinct parents produce non-interchangeable Slice<> trees —
// silently mixing Slice<MyData, 0> with Slice<ChannelTag<int>, 0>
// is a compile-time mismatch, not a runtime bug.
static_assert(!std::is_same_v<
    Slice<MyData, 0>, Slice<ChannelTag<int>, 0>>);

// ── 16-shard splits_into_pack stress (the in-header sentinels
//                                      stop at N=4) ────────────────

template <std::size_t... Is>
constexpr bool stress_pack_(std::index_sequence<Is...>) noexcept {
    return splits_into_pack_v<MyData, Slice<MyData, Is>...>;
}

static_assert(stress_pack_<>(std::make_index_sequence<16>{}));
static_assert(stress_pack_<>(std::make_index_sequence<32>{}));

// ── Runtime exercise — calls the header's smoke test which itself
//                       runs permission_root_mint → split_n → drop.

void run_runtime_smoke() {
    runtime_smoke_test();

    // Larger split exercised here (the header keeps its smoke at N=4
    // to stay readable; the sentinel TU pushes it further).
    auto parent = permission_root_mint<MyData>();
    auto eight = permission_split_n<
        Slice<MyData, 0>, Slice<MyData, 1>,
        Slice<MyData, 2>, Slice<MyData, 3>,
        Slice<MyData, 4>, Slice<MyData, 5>,
        Slice<MyData, 6>, Slice<MyData, 7>>(std::move(parent));

    static_assert(std::is_same_v<
        decltype(eight),
        auto_split_n_permissions_t<MyData, 8>>);

    // Children destruct at scope end — no permission leak surfaces
    // because the linear discipline forbids re-use anyway.
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
