#pragma once

// ── crucible::fixy — AllStrict.h (FIXY-A-PLUS-5) ──────────────────────
//
// DRY helper: the canonical 20-tag `accept_default_strict_for<dim::X>`
// pack that ~90% of fixy bindings reach for.  Pre-A-PLUS-5 this pack
// was written verbatim 9 times across the codebase (test fixtures,
// Reject.h self-tests, future Phase B stance aliases) — ~200 LoC of
// pure repetition.
//
// AllStrictAcceptPack is a std::tuple of the 20 acks in dim-enumerator
// order.  `unpack_into_IsAccepted<Pack>` is the metafunction that
// feeds a tuple through IsAccepted via std::index_sequence (concepts
// cannot be invoked through std::apply directly — they're not
// callable entities, so the standard apply-pattern doesn't compose).
//
// Downstream uses:
//
//   * Reject.h self_test block uses unpack_into_IsAccepted to verify
//     the canonical pack accepts.
//   * test_fixy_engaged scenarios that want "everything strict except
//     a few" use `replace_accept_in_pack<Pack, dim::X, NewGrant>`.
//   * Phase B `stance::PureLinear` is an alias to AllStrictAcceptPack
//     plus zero relaxations.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — std::tuple of 20 empty `accept_default_strict_for<D>`
//                tags; tuple aggregate-init zero-state per dim.  No
//                uninit state can be reached.
//   TypeSafe   — uses dim::DimAxis (strong enum) as a non-type template
//                parameter throughout.  replace_accept_in_pack matches
//                on `relaxes == Target` (enum equality).
//   NullSafe   — no pointer members.
//   MemSafe    — std::tuple of empty types stays at 1 byte total
//                under EBO; zero heap allocation.
//   BorrowSafe — pure metadata; tuple is value-domain only.
//   ThreadSafe — entire header is compile-time machinery; no runtime
//                state.
//   LeakSafe   — zero-state types; no resource.
//   DetSafe    — `unpack_into_IsAccepted_v` and `replace_accept_in_pack`
//                are consteval-callable and produce bit-identical
//                output across compiles.

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── AllStrictAcceptPack — 20 accept_default_strict_for tags ────────
// ═════════════════════════════════════════════════════════════════════
//
// Ordered by dim::DimAxis enumerator value (Type=0 → Staleness=19) so
// Phase B's row_hash_contribution can fold over the tuple in canonical
// order.

using AllStrictAcceptPack = std::tuple<
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(std::tuple_size_v<AllStrictAcceptPack> == dim::count_v);

// ═════════════════════════════════════════════════════════════════════
// ── unpack_into_IsAccepted<Pack> — feed a tuple through IsAccepted ─
// ═════════════════════════════════════════════════════════════════════
//
// Concepts are not callable.  std::apply([](auto...){return Concept
// <decltype(...)>;}, tuple) doesn't compose because the concept must
// appear in a constant-expression position.  We unfold via
// std::index_sequence + std::tuple_element and a constexpr bool
// helper.

namespace detail {

template <typename Pack, std::size_t... Is>
[[nodiscard]] consteval bool is_accepted_unpack_impl(
    std::index_sequence<Is...>) noexcept {
    return IsAccepted<std::tuple_element_t<Is, Pack>...>;
}

}  // namespace detail

template <typename Pack>
inline constexpr bool unpack_into_IsAccepted_v =
    detail::is_accepted_unpack_impl<Pack>(
        std::make_index_sequence<std::tuple_size_v<Pack>>{});

// ═════════════════════════════════════════════════════════════════════
// ── replace_accept_in_pack<Pack, D, NewTag> — swap one accept tag ──
// ═════════════════════════════════════════════════════════════════════
//
// Common pattern in test fixtures: take AllStrictAcceptPack and replace
// the accept_default_strict_for<D> entry with a relaxation tag (or
// remove it for the dim-omission neg-compile fixtures).  The
// metafunction below builds the replacement tuple structurally.
//
// (Pre-A-PLUS-5 each scenario inlined the 20 tags with one swapped —
// 9 sites × 20 tags = 180 LoC of pure copy-paste.)

namespace detail {

template <dim::DimAxis Target, typename NewTag, typename Pack,
          std::size_t... Is>
consteval auto replace_pack_impl(std::index_sequence<Is...>)
    -> std::tuple<
        std::conditional_t<
            std::tuple_element_t<Is, Pack>::relaxes == Target,
            NewTag,
            std::tuple_element_t<Is, Pack>>...>;

}  // namespace detail

template <dim::DimAxis Target, typename NewTag, typename Pack = AllStrictAcceptPack>
using replace_accept_in_pack = decltype(detail::replace_pack_impl<
    Target, NewTag, Pack>(
        std::make_index_sequence<std::tuple_size_v<Pack>>{}));

// ── Sanity self-tests ────────────────────────────────────────────────
namespace detail_test {

static_assert(unpack_into_IsAccepted_v<AllStrictAcceptPack>,
    "Canonical all-strict pack must satisfy IsAccepted.");

// Replace dim::Usage's accept with grant::copy — still IsAccepted.
using CopyUsagePack = replace_accept_in_pack<dim::Usage, grant::copy>;
static_assert(std::is_same_v<std::tuple_element_t<2, CopyUsagePack>, grant::copy>,
    "Usage slot (index 2) must hold the replacement grant.");
static_assert(unpack_into_IsAccepted_v<CopyUsagePack>,
    "Pack with one grant::copy + 19 acks must satisfy IsAccepted.");

}  // namespace detail_test

}  // namespace crucible::fixy
