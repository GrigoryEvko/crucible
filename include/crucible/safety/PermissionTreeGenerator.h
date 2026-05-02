#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::PermissionTreeGenerator — auto-generated
// permission trees for any (Parent, N) parallel-fan-out pattern.
//
// FOUND-A21 / 27_04_2026.md §5.4 home.  Provides:
//
//   * Slice<Parent, I>                 phantom indexed sub-tag
//   * splits_into_pack<Parent,
//                      Slice<Parent, Is>...>
//                                      auto-specialization (any Is...)
//   * auto_split_n<Parent, N>::type    convenience type alias
//                                      (std::tuple<Slice<Parent, 0..N-1>>)
//   * can_split_n_v<Parent, N>         consteval bool
//
// This header is the single point of truth for the 1D auto-permission
// tree.  OwnedRegion.h, PermissionedShardedGrid (FOUND-A11), the
// signature dispatcher's auto-tree extraction (FOUND-D11), and any
// future N-way fan-out primitive include this header to obtain the
// generator without pulling in OwnedRegion's full API surface.
//
// ─── Why a dedicated header ────────────────────────────────────────
//
// Slice + the index-pack splits_into_pack specialization originally
// shipped inside OwnedRegion.h because OwnedRegion was the first
// consumer.  The architectural rule (27_04_2026.md §1.5 — "the
// dispatcher reads from signatures, never from where the type
// happens to live") demands a dedicated home so:
//
//   * PermissionedShardedGrid (M producers × N consumers) consumes
//     `auto_split_n<Whole, M+N>` without including OwnedRegion.
//   * The signature-extract layer (FOUND-D02..D11) can detect Slice
//     in parameter types via reflection without OwnedRegion in scope.
//   * Future 2D / 3D variants (FOUND-A22) live alongside the 1D
//     generator without circular includes.
//
// ─── The mechanism ─────────────────────────────────────────────────
//
// One partial specialization handles ALL N values via index-pack
// deduction:
//
//   template <typename Parent, std::size_t... Is>
//   struct splits_into_pack<Parent, Slice<Parent, Is>...>
//       : std::true_type {};
//
// mint_permission_split_n<Slice<Parent, Is>...> succeeds for any (Parent,
// sizeof...(Is)) pair without per-N user-side declaration.  The
// permission factory checks the trait via splits_into_pack_v; the
// trait fires for any number of distinct Slice<Parent, I> children.
//
// ─── Reflection-friendliness ───────────────────────────────────────
//
// P2996R13 display_string_of(^^Slice<Parent, I>) produces the
// canonical type name "Slice<Parent, I>" — no mangled symbol — which
// the diagnostic infrastructure (FOUND-E01..E04) consumes for
// human-readable error messages.  No additional naming machinery
// needed in this header; the canonical form is already P2996-stable.
//
// ─── Composition with OwnedRegion ──────────────────────────────────
//
// OwnedRegion<T, Tag>::split_into<N>() returns a
// std::tuple<OwnedRegion<T, Slice<Tag, Is>>...> — one sub-region per
// shard.  Each sub-region's Permission<Slice<Tag, I>> token is
// derived via mint_permission_split_n, validated against the auto-
// generated splits_into_pack specialization in this header.
//
// PermissionedShardedGrid<UserTag, M, N> reuses the same Slice<>
// template at distinct indices — Producer slots are
// Slice<Whole<UserTag>, 0..M-1>, Consumer slots are
// Slice<Whole<UserTag>, M..M+N-1>.  No new tag types required.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Slice<Parent, I> — phantom indexed sub-tag ─────────────────────
//
// One distinct type per (Parent, I) pair.  Carries no data; pure
// compile-time identity.  The splits_into_pack partial specialization
// below proves disjointness for the Slice<Parent, 0..N-1> pattern.
//
// `parent_type` and `index` are exposed for reflection-driven
// diagnostics and for primitives that need to recover the parent tag
// from a Slice (e.g., the signature dispatcher when it sees an
// OwnedRegion<T, Slice<UserTag, 3>> parameter).

template <typename Parent, std::size_t I>
struct Slice {
    using parent_type = Parent;
    static constexpr std::size_t index = I;
};

// One partial specialization handles ALL N values via index-pack
// deduction.  mint_permission_split_n<Slice<Parent, Is>...> succeeds for
// any (Parent, sizeof...(Is)) pair without per-N user-side work.

template <typename Parent, std::size_t... Is>
struct splits_into_pack<Parent, Slice<Parent, Is>...>
    : std::true_type {};

// ── auto_split_n<Parent, N>::type ──────────────────────────────────
//
// Convenience alias producing the heterogeneous tuple of N distinct
// Slice<Parent, 0..N-1> tag types.  Consumed by primitives that need
// to construct N child Permission tokens at once via
// mint_permission_split_n.
//
// Usage:
//   using slice_pack_t = auto_split_n<MyTag, 8>::type;
//       // = std::tuple<Slice<MyTag, 0>, Slice<MyTag, 1>, ...,
//       //              Slice<MyTag, 7>>

namespace detail {

template <typename Parent, std::size_t... Is>
constexpr auto auto_split_tuple_(std::index_sequence<Is...>) noexcept
    -> std::tuple<Slice<Parent, Is>...>;

template <typename Parent, std::size_t... Is>
constexpr auto auto_split_perms_(std::index_sequence<Is...>) noexcept
    -> std::tuple<Permission<Slice<Parent, Is>>...>;

}  // namespace detail

template <typename Parent, std::size_t N>
struct auto_split_n {
    static_assert(N > 0,
        "auto_split_n<Parent, N>: N must be greater than zero — "
        "splitting a permission into zero shards has no operational meaning");

    // Tuple of the slice tag types themselves (compile-time identity only).
    using type = decltype(
        detail::auto_split_tuple_<Parent>(std::make_index_sequence<N>{}));

    // Tuple of the slice Permission tokens (the actual return type of
    // mint_permission_split_n<Slice<Parent, Is>...>(parent_perm)).
    using permissions_type = decltype(
        detail::auto_split_perms_<Parent>(std::make_index_sequence<N>{}));
};

template <typename Parent, std::size_t N>
using auto_split_n_t = typename auto_split_n<Parent, N>::type;

template <typename Parent, std::size_t N>
using auto_split_n_permissions_t =
    typename auto_split_n<Parent, N>::permissions_type;

// ── can_split_n_v<Parent, N> — consteval check ─────────────────────
//
// Resolves to true iff the auto-generated splits_into_pack
// specialization in this header would fire for (Parent, N).  Today
// this is unconditionally true for any N > 0 because the
// specialization is universal — but the trait is published as the
// public capability check so future refinements (e.g., refusing N
// above an architectural ceiling) have a stable extension point.

namespace detail {

template <typename Parent, std::size_t... Is>
constexpr bool can_split_n_impl_(std::index_sequence<Is...>) noexcept {
    return splits_into_pack_v<Parent, Slice<Parent, Is>...>;
}

}  // namespace detail

template <typename Parent, std::size_t N>
inline constexpr bool can_split_n_v =
    (N > 0) && detail::can_split_n_impl_<Parent>(std::make_index_sequence<N>{});

// ── Sentinel static_asserts (per project memory: header-only TUs ───
//                             must be exercised by an including .cpp,
//                             see test/test_permission_tree_generator.cpp)

namespace detail {
struct ptg_test_tag_ {};
}  // namespace detail

static_assert(sizeof(Slice<detail::ptg_test_tag_, 0>) == 1,
    "Slice<Parent, I>: must be a 1-byte empty class (no payload)");
static_assert(std::is_trivially_destructible_v<Slice<detail::ptg_test_tag_, 0>>);
static_assert(std::is_empty_v<Slice<detail::ptg_test_tag_, 0>>);

// Distinct (Parent, I) pairs produce distinct types.
static_assert(!std::is_same_v<Slice<detail::ptg_test_tag_, 0>,
                              Slice<detail::ptg_test_tag_, 1>>);

// parent_type and index round-trip.
static_assert(std::is_same_v<Slice<detail::ptg_test_tag_, 5>::parent_type,
                             detail::ptg_test_tag_>);
static_assert(Slice<detail::ptg_test_tag_, 5>::index == 5);

// splits_into_pack auto-fires for any N >= 1.
static_assert(splits_into_pack_v<detail::ptg_test_tag_,
                                 Slice<detail::ptg_test_tag_, 0>>);
static_assert(splits_into_pack_v<detail::ptg_test_tag_,
                                 Slice<detail::ptg_test_tag_, 0>,
                                 Slice<detail::ptg_test_tag_, 1>,
                                 Slice<detail::ptg_test_tag_, 2>,
                                 Slice<detail::ptg_test_tag_, 3>>);

// can_split_n_v matches.
static_assert(can_split_n_v<detail::ptg_test_tag_, 1>);
static_assert(can_split_n_v<detail::ptg_test_tag_, 4>);
static_assert(can_split_n_v<detail::ptg_test_tag_, 64>);

// auto_split_n_t is the homogeneous-shape but heterogeneous-types tuple.
static_assert(std::is_same_v<
    auto_split_n_t<detail::ptg_test_tag_, 3>,
    std::tuple<Slice<detail::ptg_test_tag_, 0>,
               Slice<detail::ptg_test_tag_, 1>,
               Slice<detail::ptg_test_tag_, 2>>>);

static_assert(std::is_same_v<
    auto_split_n_permissions_t<detail::ptg_test_tag_, 2>,
    std::tuple<Permission<Slice<detail::ptg_test_tag_, 0>>,
               Permission<Slice<detail::ptg_test_tag_, 1>>>>);

// ── Runtime smoke test (per project memory: every header ships an ──
//                       inline runtime witness using non-constant
//                       arguments + the actual factory chain)

inline void runtime_smoke_test() {
    // Mint a parent permission, split it into 4 child Slice<> tokens.
    using Tag = detail::ptg_test_tag_;
    auto parent = mint_permission_root<Tag>();

    auto children = mint_permission_split_n<
        Slice<Tag, 0>, Slice<Tag, 1>,
        Slice<Tag, 2>, Slice<Tag, 3>>(std::move(parent));

    // The tuple types match auto_split_n_permissions_t.
    static_assert(std::is_same_v<
        decltype(children),
        auto_split_n_permissions_t<Tag, 4>>);

    // Suppress unused-variable warnings; children destruct at scope end.
    (void)children;
}

}  // namespace crucible::safety
