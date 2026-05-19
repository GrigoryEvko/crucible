#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::PermissionGridGenerator — 2D auto-permission-tree
// for M producers × N consumers grids.
//
// FOUND-A22 / 27_04_2026.md §5.4 (the 2D extension).  Required by:
//
//   * PermissionedShardedGrid (FOUND-A11/A12) — M × N SpscRing
//     mesh where each producer owns one column and each consumer
//     owns one row.
//   * Any future scatter/gather primitive that needs distinct
//     producer-vs-consumer roles per shard.
//
// ─── Why TWO-LEVEL 1D, not one giant pack ──────────────────────────
//
// The naive "flat M+N pack" specialization
//
//   template <typename Whole, std::size_t... Is, std::size_t... Js>
//   struct splits_into_pack<Whole,
//                           Producer<Whole, Is>...,
//                           Consumer<Whole, Js>...>
//       : std::true_type {};
//
// runs into the C++ partial-specialization rule that two unrelated
// packs can't both be deduced when they share a class-template
// boundary.  GCC may or may not handle it today, and even if it does
// the diagnostic surface is murky.
//
// The clean construction is two-level:
//
//   step 1 (binary):  Whole → ProducerSide<Whole> + ConsumerSide<Whole>
//   step 2 (1D each): ProducerSide<Whole> → M Slice<ProducerSide<Whole>, I>
//                     ConsumerSide<Whole> → N Slice<ConsumerSide<Whole>, J>
//
// Both steps are existing primitives:
//   * step 1 uses splits_into / mint_permission_split (binary)
//   * step 2 uses splits_into_pack / mint_permission_split_n via the
//     1D Slice mechanism in safety/PermissionTreeGenerator.h
//
// The 2D-ness is conceptual — the permission system sees M+N
// disjoint sub-regions, distinguishable by their parent side.  No
// new partial specialization of splits_into_pack is required.
//
// ─── Public API surface ────────────────────────────────────────────
//
//   ProducerSide<Whole>             phantom side tag
//   ConsumerSide<Whole>             phantom side tag
//
//   Producer<Whole, I>              alias for Slice<ProducerSide<Whole>, I>
//   Consumer<Whole, J>              alias for Slice<ConsumerSide<Whole>, J>
//
//   auto_split_grid<Whole, M, N>    type-level descriptor (tuples of
//                                   tags + tuples of permission
//                                   tokens for both sides)
//
//   GridPermissions<Whole, M, N>    runtime carrier of M producer
//                                   permissions + N consumer permissions
//
//   mint_grid_permissions<Whole, M, N>(parent)
//                                   two-level split factory (§XXI token mint)
//
//   can_split_grid_v<Whole, M, N>   consteval capability check
//                                   (§XXI requires-clause gate)
//
// ─── Diagnostic naming ─────────────────────────────────────────────
//
// P2996R13 display_string_of(^^Producer<MyData, 3>) renders as
// "crucible::safety::Slice<crucible::safety::ProducerSide<MyData>, 3>"
// — verbose but unambiguous.  The FOUND-E series diagnostic
// infrastructure can shorten this once stable_name_of lands.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionTreeGenerator.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Side tags ──────────────────────────────────────────────────────
//
// Distinct types per (Whole, side).  ProducerSide<Whole> is NEVER
// the same as ConsumerSide<Whole> — silent role-confusion is
// impossible.

template <typename Whole>
struct ProducerSide {
    using parent_type = Whole;
};

template <typename Whole>
struct ConsumerSide {
    using parent_type = Whole;
};

// ── Whole splits binary into the two sides ─────────────────────────

template <typename Whole>
struct splits_into<Whole, ProducerSide<Whole>, ConsumerSide<Whole>>
    : std::true_type {};

// fixy-M-29 authoring witness (paired with the splits_into spec above).
template <typename Whole>
struct splits_into_authoring_witness<
    Whole, ProducerSide<Whole>, ConsumerSide<Whole>>
    : std::true_type {};

// ── Per-side slot aliases (reuse 1D Slice<>) ───────────────────────
//
// A producer slot at index I is Slice<ProducerSide<Whole>, I>.  A
// consumer slot at index J is Slice<ConsumerSide<Whole>, J>.  Same
// 1D mechanism on each side — no new specialization needed.

template <typename Whole, std::size_t I>
using Producer = Slice<ProducerSide<Whole>, I>;

template <typename Whole, std::size_t J>
using Consumer = Slice<ConsumerSide<Whole>, J>;

// ── auto_split_grid<Whole, M, N> — type descriptor ─────────────────

template <typename Whole, std::size_t M, std::size_t N>
struct auto_split_grid {
    static_assert(M > 0,
        "auto_split_grid<Whole, M, N>: M (producer count) must be > 0");
    static_assert(N > 0,
        "auto_split_grid<Whole, M, N>: N (consumer count) must be > 0");

    using whole_type = Whole;
    using producer_side_type = ProducerSide<Whole>;
    using consumer_side_type = ConsumerSide<Whole>;

    static constexpr std::size_t producer_count = M;
    static constexpr std::size_t consumer_count = N;

    // Tag tuples — the slot types themselves (compile-time identity).
    using producer_tags = auto_split_n_t<ProducerSide<Whole>, M>;
    using consumer_tags = auto_split_n_t<ConsumerSide<Whole>, N>;

    // Permission tuples — what mint_grid_permissions actually returns.
    using producer_perms = auto_split_n_permissions_t<ProducerSide<Whole>, M>;
    using consumer_perms = auto_split_n_permissions_t<ConsumerSide<Whole>, N>;
};

// ── GridPermissions<Whole, M, N> — runtime carrier ─────────────────
//
// Move-only because each Permission inside is linear.  Holds the
// full M producer + N consumer permission tokens after mint_grid_permissions().

template <typename Whole, std::size_t M, std::size_t N>
struct [[nodiscard]] GridPermissions {
    static_assert(M > 0 && N > 0);

    // Aggregate — kept aggregate so designated-init / brace-init works.
    // Move-only is inherited from std::tuple<Permission<...>...> which
    // is itself move-only via the Permission tokens' deleted copy.
    typename auto_split_grid<Whole, M, N>::producer_perms producers;
    typename auto_split_grid<Whole, M, N>::consumer_perms consumers;
};

// ── can_split_grid_v<Whole, M, N> — capability check ───────────────
//
// §XXI requires-clause gate for mint_grid_permissions.  Folds M>0,
// N>0, the binary side-split fit, and per-side N-ary fit into one
// concept-shaped value trait so the factory's requires clause is a
// single capability check.

template <typename Whole, std::size_t M, std::size_t N>
inline constexpr bool can_split_grid_v =
       (M > 0)
    && (N > 0)
    && splits_into_v<Whole, ProducerSide<Whole>, ConsumerSide<Whole>>
    && can_split_n_v<ProducerSide<Whole>, M>
    && can_split_n_v<ConsumerSide<Whole>, N>;

// ── mint_grid_permissions<Whole, M, N>(parent) — §XXI token mint ───
//
// §XXI Universal Mint Pattern: a token mint that consumes a parent
// Permission<Whole> and produces a fresh authoritative
// GridPermissions<Whole, M, N> holding M producer permissions and N
// consumer permissions.  Named `mint_*` so `grep "mint_"` finds every
// authorization point in the codebase.
//
// Two-level construction: binary side-split (producer vs consumer)
// followed by per-side N-ary split.  Soundness gate is the single
// `requires can_split_grid_v<Whole, M, N>` clause, which folds M>0,
// N>0, and the per-side splits_into_pack registration in one place
// (consistent with the §XXI canonical-mints table where every token
// mint carries a single concept gate).

namespace detail {

template <typename Whole, std::size_t... Is>
[[nodiscard]] constexpr auto split_producer_side_(
    Permission<ProducerSide<Whole>>&& side,
    std::index_sequence<Is...>) noexcept
{
    return mint_permission_split_n<Slice<ProducerSide<Whole>, Is>...>(
        std::move(side));
}

template <typename Whole, std::size_t... Js>
[[nodiscard]] constexpr auto split_consumer_side_(
    Permission<ConsumerSide<Whole>>&& side,
    std::index_sequence<Js...>) noexcept
{
    return mint_permission_split_n<Slice<ConsumerSide<Whole>, Js>...>(
        std::move(side));
}

}  // namespace detail

template <typename Whole, std::size_t M, std::size_t N>
    requires can_split_grid_v<Whole, M, N>
[[nodiscard]] constexpr auto mint_grid_permissions(Permission<Whole>&& parent) noexcept
    -> GridPermissions<Whole, M, N>
{
    auto sides = mint_permission_split<ProducerSide<Whole>, ConsumerSide<Whole>>(
        std::move(parent));

    return GridPermissions<Whole, M, N>{
        .producers = detail::split_producer_side_<Whole>(
            std::move(sides.first), std::make_index_sequence<M>{}),
        .consumers = detail::split_consumer_side_<Whole>(
            std::move(sides.second), std::make_index_sequence<N>{}),
    };
}

// ── Sentinel static_asserts ────────────────────────────────────────

namespace detail {
struct grid_test_tag_ {};
}  // namespace detail

// Side tags are 1-byte empty classes.
static_assert(sizeof(ProducerSide<detail::grid_test_tag_>) == 1);
static_assert(sizeof(ConsumerSide<detail::grid_test_tag_>) == 1);
static_assert(std::is_empty_v<ProducerSide<detail::grid_test_tag_>>);
static_assert(std::is_empty_v<ConsumerSide<detail::grid_test_tag_>>);

// Producer<Whole, I> and Consumer<Whole, J> alias to distinct Slice<>.
static_assert(std::is_same_v<
    Producer<detail::grid_test_tag_, 0>,
    Slice<ProducerSide<detail::grid_test_tag_>, 0>>);
static_assert(std::is_same_v<
    Consumer<detail::grid_test_tag_, 0>,
    Slice<ConsumerSide<detail::grid_test_tag_>, 0>>);

// Producer side and consumer side of the same Whole are NOT
// interchangeable — the type system enforces the role distinction.
static_assert(!std::is_same_v<
    Producer<detail::grid_test_tag_, 0>,
    Consumer<detail::grid_test_tag_, 0>>);

// Whole's binary split is registered.
static_assert(splits_into_v<
    detail::grid_test_tag_,
    ProducerSide<detail::grid_test_tag_>,
    ConsumerSide<detail::grid_test_tag_>>);

// can_split_grid_v matches at multiple (M, N) tuples.
static_assert(can_split_grid_v<detail::grid_test_tag_, 1, 1>);
static_assert(can_split_grid_v<detail::grid_test_tag_, 4, 4>);
static_assert(can_split_grid_v<detail::grid_test_tag_, 8, 16>);
static_assert(!can_split_grid_v<detail::grid_test_tag_, 0, 4>);
static_assert(!can_split_grid_v<detail::grid_test_tag_, 4, 0>);

// auto_split_grid surfaces both side types correctly.
static_assert(std::is_same_v<
    auto_split_grid<detail::grid_test_tag_, 2, 3>::producer_tags,
    std::tuple<Producer<detail::grid_test_tag_, 0>,
               Producer<detail::grid_test_tag_, 1>>>);
static_assert(std::is_same_v<
    auto_split_grid<detail::grid_test_tag_, 2, 3>::consumer_tags,
    std::tuple<Consumer<detail::grid_test_tag_, 0>,
               Consumer<detail::grid_test_tag_, 1>,
               Consumer<detail::grid_test_tag_, 2>>>);

// ── Runtime smoke test ─────────────────────────────────────────────

inline void runtime_smoke_test_grid() {
    using Tag = detail::grid_test_tag_;

    // Mint, split into 4-producer × 3-consumer grid.
    auto whole = mint_permission_root<Tag>();
    auto grid = mint_grid_permissions<Tag, 4, 3>(std::move(whole));

    static_assert(std::is_same_v<
        decltype(grid),
        GridPermissions<Tag, 4, 3>>);
    static_assert(std::is_same_v<
        decltype(grid.producers),
        std::tuple<Permission<Producer<Tag, 0>>,
                   Permission<Producer<Tag, 1>>,
                   Permission<Producer<Tag, 2>>,
                   Permission<Producer<Tag, 3>>>>);
    static_assert(std::is_same_v<
        decltype(grid.consumers),
        std::tuple<Permission<Consumer<Tag, 0>>,
                   Permission<Consumer<Tag, 1>>,
                   Permission<Consumer<Tag, 2>>>>);

    // Suppress unused; permissions destruct at scope end.
    (void)grid;
}

}  // namespace crucible::safety
