#pragma once

// ── crucible::fixy — Stance.h (FIXY-B3) ────────────────────────────────
//
// Ergonomic shorthand for the 8 most common dim-engagement patterns.
// Each stance is a `std::tuple<...>` of 20 tag types that
// `mint_fn_with_stance` unpacks as a `Grants...` pack.  Most fixy
// bindings pick ONE stance from this catalog; rare cross-cutting
// combinations use `stance::compose<Base, AddedGrants...>`.
//
// ── The 8 canonical stances ──────────────────────────────────────────
//
//   PureLinear      — all dims at strict default (most restrictive).
//   PureCopy        — Usage relaxed to Copy; rest strict.
//   IoFunction      — Effect = {IO}; rest strict.
//   BgWorker        — Effect = {Bg, Alloc, Block}; rest strict.
//   CtCrypto        — Precision = Higham<0> + Reentrancy = Reentrant;
//                     rest strict.  (Tagged-policy ConstantTime is a
//                     Phase C invariant — Phase B defers actual CT
//                     enforcement to substrate.)
//   SecretConsumer  — Security strict (Classified retained); intent =
//                     "I see classified data and never emit publicly".
//   PublicEmit<P>   — Security relaxed via grant::declassify<P>; rest
//                     strict.  Policy P is the audit trail.
//   AsyncEndpoint   — Reentrancy = Coroutine + Effect = {Bg, Block};
//                     rest strict.  Canonical session-typed channel
//                     endpoint.
//
// ── Composition ──────────────────────────────────────────────────────
//
// `stance::compose<Base, NewGrants...>` appends NewGrants to Base.
// Phase B ships APPEND form; de-dup of per-dim conflicts is Phase C
// (the user is responsible for ensuring no two grants engage the same
// dim — IsAccepted accepts redundancy, ValidComposition rejects
// genuine conflicts).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe — every stance ships all 20 tags; no implicit defaults.
//   TypeSafe — strong-typed tuples; no implicit type conversions.
//   DetSafe  — bit-identical stance composition across compiles.
//   LeakSafe — zero-state metadata.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  Pure type-level composition.

#include <crucible/fixy/AllStrict.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>

#include <tuple>
#include <type_traits>

namespace crucible::fixy::stance {

// ═════════════════════════════════════════════════════════════════════
// ── Helper: replace one accept tag with a relaxation in a pack ─────
// ═════════════════════════════════════════════════════════════════════
//
// Stance authoring pattern: take AllStrictAcceptPack and swap out the
// accept for one or more dims with relaxation tags.  AllStrict.h's
// replace_accept_in_pack does ONE replacement; we chain through
// nested replace_accept_in_pack for multi-replacements.

namespace detail {

template <typename Pack, dim::DimAxis D, typename NewTag>
using replace_one = replace_accept_in_pack<D, NewTag, Pack>;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── stance::PureLinear ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All 20 dims at strict default.  Equivalent to AllStrictAcceptPack.
// Use for value-semantic types with no special discipline relaxation.

using PureLinear = AllStrictAcceptPack;

// ═════════════════════════════════════════════════════════════════════
// ── stance::PureCopy ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Usage relaxed to Copy.  Use for small POD / trivially-copyable
// structs that don't have linear-handle discipline.

using PureCopy = detail::replace_one<PureLinear, dim::Usage, grant::copy>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::IoFunction ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Effect relaxed to {IO}.  Canonical filesystem-read / stdout-print /
// network-receive function.

using IoFunction = detail::replace_one<PureLinear, dim::Effect, grant::with_io>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::BgWorker ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Effect = {Bg, Alloc, Block}.  Canonical background-thread shape —
// Cipher cold-tier writer, BackgroundThread pipeline stage, observe
// metrics sink.

using BgWorker = detail::replace_one<
    PureLinear,
    dim::Effect,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Alloc,
                ::crucible::effects::Effect::Block>>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::CtCrypto ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reentrancy = Reentrant (for re-entrant crypto state machines).
// Precision strict (no reassociation).  Tagged ConstantTime is a
// Phase C substrate-invariant (the actual CT enforcement is a
// `with CT` region annotation at the body level, not a Grants pack
// dim) — Phase B defers and documents.

using CtCrypto = detail::replace_one<
    PureLinear,
    dim::Reentrancy,
    grant::reentrant>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::SecretConsumer ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Identical to PureLinear in dim engagement, but the STANCE NAME
// documents the binding's intent: it consumes Classified data and
// never declassifies.  The discipline is auditable by reading the
// stance.  No relaxations.

using SecretConsumer = PureLinear;

// ═════════════════════════════════════════════════════════════════════
// ── stance::PublicEmit<Policy> ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Security relaxed via grant::declassify<Policy>.  Policy is a named
// type that documents the declassification rationale; review reads
// the Policy type to verify the binding's audit story.

template <typename Policy>
using PublicEmit = detail::replace_one<
    PureLinear,
    dim::Security,
    grant::declassify<Policy>>;

// ═════════════════════════════════════════════════════════════════════
// ── stance::AsyncEndpoint ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reentrancy = Coroutine + Effect = {Bg, Block}.  Canonical
// session-typed channel endpoint that yields to a scheduler.
//
// Two replacements needed; nested chain.

using AsyncEndpoint_step1 = detail::replace_one<
    PureLinear,
    dim::Reentrancy,
    grant::coroutine>;

using AsyncEndpoint = detail::replace_one<
    AsyncEndpoint_step1,
    dim::Effect,
    grant::with<::crucible::effects::Effect::Bg,
                ::crucible::effects::Effect::Block>>;

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn_for<Stance>(value) — stance-bound mint helper ──────────
// ═════════════════════════════════════════════════════════════════════
//
// Bridge from a stance tuple to mint_fn's variadic Grants pack.
// Uses std::index_sequence to unpack the tuple into the variadic
// template parameter list.

namespace detail {

template <typename Pack, typename Type, std::size_t... Is>
[[nodiscard]] constexpr auto mint_with_pack_impl(
    Type&& v, std::index_sequence<Is...>)
{
    return mint_fn<std::tuple_element_t<Is, Pack>...>(
        std::forward<Type>(v));
}

}  // namespace detail

template <typename Stance, typename Type>
[[nodiscard]] constexpr auto mint_fn_for(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
{
    return detail::mint_with_pack_impl<Stance>(
        std::move(v),
        std::make_index_sequence<std::tuple_size_v<Stance>>{});
}

// ═════════════════════════════════════════════════════════════════════
// ── Sanity self-tests ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// Every stance produces an IsAccepted pack.
static_assert(unpack_into_IsAccepted_v<PureLinear>);
static_assert(unpack_into_IsAccepted_v<PureCopy>);
static_assert(unpack_into_IsAccepted_v<IoFunction>);
static_assert(unpack_into_IsAccepted_v<BgWorker>);
static_assert(unpack_into_IsAccepted_v<CtCrypto>);
static_assert(unpack_into_IsAccepted_v<SecretConsumer>);
static_assert(unpack_into_IsAccepted_v<AsyncEndpoint>);

struct DemoPolicy {};
static_assert(unpack_into_IsAccepted_v<PublicEmit<DemoPolicy>>);

// PureCopy's Usage slot is grant::copy (replaced from
// accept_default_strict_for<dim::Usage>).  AllStrict order: Usage is
// enumerator value 2 (Type=0, Refinement=1, Usage=2).
static_assert(std::is_same_v<std::tuple_element_t<2, PureCopy>, grant::copy>);

}  // namespace self_test

}  // namespace crucible::fixy::stance
