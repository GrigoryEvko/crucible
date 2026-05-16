#pragma once

// ── crucible::fixy — Fn.h (FIXY-B2) ────────────────────────────────────
//
// The user-facing aggregator: `fixy::fn<Type, Grants...>` is the
// reject-by-default surface that ties together Phase A's engagement
// gate (IsAccepted), Phase B's resolver (resolved_fn_t), and the
// substrate's `safety::fn::Fn<...>` template body.
//
// **Two-layer gate.** A fixy::fn instantiation fires TWO compile-time
// checks before producing a usable type:
//
//   1. `IsAccepted<Grants...>` — Phase A.  Every dim engaged.  Failure
//      → "FixyNotEngaged_<DimName>" via the static_assert message.
//   2. `safety::fn::ValidComposition<underlying_fn_t>` — substrate.
//      The 12 §6.8 collision rules fire when the resolved
//      `safety::fn::Fn<...>` instantiation violates a known cross-axis
//      unsoundness pattern (I002, L002, E044, ...).
//
// **EBO-zero cost.** `sizeof(fixy::fn<T, Grants...>) == sizeof(T)`.
// The underlying safety::fn::Fn carries a single Type member; fixy::fn
// just forwards.  Static_assert at file end pins this.
//
// **Universal mint pattern.** `mint_fn<Grants...>(value)` per
// CLAUDE.md §XXI.  Token-mint flavor — authority derives from the
// caller-supplied Grants pack, no Ctx parameter.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <typename Type, typename... Grants>
//   struct fn {
//     static_assert(IsAccepted<Grants...>, "...");
//     using underlying_fn_t = resolve::resolved_fn_t<Type, Grants...>;
//     underlying_fn_t value_;
//     ...
//   };
//
//   template <typename... Grants, typename Type>
//   [[nodiscard]] constexpr fn<Type, Grants...> mint_fn(Type v) noexcept
//     requires IsAccepted<Grants...>;
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe   — Type value_ is the only runtime member; inherits T's
//                NSDMI or requires brace-init.
//   TypeSafe   — strong-typed grade vector via Resolve.h dispatch.
//   NullSafe   — no pointer members.
//   MemSafe    — defaulted copy/move; no allocation; ownership = T's.
//   BorrowSafe — no cross-thread mutation; pure value-semantic wrapper.
//   ThreadSafe — per-thread soundness of fixy::fn IS per-thread
//                soundness of T (no atomics added by the wrapper).
//   LeakSafe   — no resources beyond T.
//   DetSafe    — every operation constexpr; 20-axis grade is a STATIC
//                property; same Grants pack → same underlying Fn type.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// `sizeof(fixy::fn<T, ...>) == sizeof(T)` (via underlying safety::fn::Fn).
// Construction = T's ctor.  Move/copy = T's.  Accessors are zero-cost
// forwarders.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase B   — Fn aggregator deliverable
//   safety/Fn.h                          — substrate Fn template + ValidComposition
//   fixy/Reject.h                        — IsAccepted concept gate
//   fixy/Resolve.h                       — Grants → Fn parameter resolver
//   CLAUDE.md §XXI                       — Universal mint pattern

#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Resolve.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/NotInherited.h>

#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── fixy::fn — the user-facing aggregator ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename Type, typename... Grants>
struct fn final {
    // ─── Two-layer gate ────────────────────────────────────────────
    //
    // Gate 1: Phase A engagement check.  Every dim must be engaged
    // via grant::* OR accept_default_strict_for<dim::X>.  The
    // diagnostic message carries the dim-name lookup hint.
    static_assert(IsAccepted<Grants...>,
        "fixy::fn rejects: the Grants pack must engage all 20 dims. "
        "Read fixy::diag::FixyNotEngaged_<DimName>::description for the "
        "missing engagement (where <DimName> is the first dim returned "
        "by WhichDimUnengaged<Grants...>::value).  Add "
        "accept_default_strict_for<dim::X> for strict-default acknowledgement, "
        "or a grant::* relaxation tag.");

    // ─── Underlying substrate Fn (gate 2 fires inside this) ────────
    //
    // The substrate's Fn template body ships a static_assert on
    // `ValidComposition<Fn>` — the 12 §6.8 collision rules fire HERE
    // if the resolved Grants composition violates a cross-axis
    // unsoundness pattern.  fixy::fn inherits the rejection
    // automatically.
    using underlying_fn_t = resolve::resolved_fn_t<Type, Grants...>;

    // ─── Per-axis introspection forwarders ─────────────────────────
    //
    // Mirror Fn's surface so a downstream consumer can read a fixy::fn
    // exactly as they'd read a safety::fn::Fn.
    using type_t        = typename underlying_fn_t::type_t;
    using refinement_t  = typename underlying_fn_t::refinement_t;
    using effect_row_t  = typename underlying_fn_t::effect_row_t;
    using protocol_t    = typename underlying_fn_t::protocol_t;
    using lifetime_t    = typename underlying_fn_t::lifetime_t;
    using source_t      = typename underlying_fn_t::source_t;
    using trust_t       = typename underlying_fn_t::trust_t;
    using cost_t        = typename underlying_fn_t::cost_t;
    using precision_t   = typename underlying_fn_t::precision_t;
    using space_t       = typename underlying_fn_t::space_t;
    using size_t_       = typename underlying_fn_t::size_t_;
    using staleness_t   = typename underlying_fn_t::staleness_t;

    // `[[maybe_unused]]` because per-axis forwarders are introspection
    // aliases; consumers may read only the axis(es) they care about.
    // Without the attribute, `-Wunused-const-variable=2` would fire
    // for every unread axis.
    [[maybe_unused]] static constexpr auto usage_v       = underlying_fn_t::usage_v;
    [[maybe_unused]] static constexpr auto security_v    = underlying_fn_t::security_v;
    [[maybe_unused]] static constexpr auto repr_v        = underlying_fn_t::repr_v;
    [[maybe_unused]] static constexpr auto overflow_v    = underlying_fn_t::overflow_v;
    [[maybe_unused]] static constexpr auto mutation_v    = underlying_fn_t::mutation_v;
    [[maybe_unused]] static constexpr auto reentrancy_v  = underlying_fn_t::reentrancy_v;
    [[maybe_unused]] static constexpr auto version_v     = underlying_fn_t::version_v;

    // ─── Sole runtime member ───────────────────────────────────────
    underlying_fn_t value_{};

    // ─── Construction ──────────────────────────────────────────────
    constexpr fn() = default;

    explicit constexpr fn(Type v)
        noexcept(std::is_nothrow_constructible_v<underlying_fn_t, Type>)
        : value_{std::move(v)} {}

    // ─── Value access (deducing this for const/non-const) ──────────
    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_.value();
    }

    // ─── call(Args...) — FIXY-G2 value-grade-aligned invocation ────
    //
    // Forwards to the underlying value's callable.  Return type is
    // `value_t<fn>` — today that's `type_t` (the substrate-resolved
    // underlying type); future iterations may wrap the return in
    // outer-axis substrate wrappers per canonical-nesting order.
    //
    // The bare `.value()()` direct-invocation form is preserved as
    // backwards-compat — call sites that don't care about value-axis
    // alignment can keep using it.
    template <typename Self, typename... Args>
    constexpr decltype(auto) call(this Self&& self, Args&&... args)
        noexcept(noexcept(std::forward<Self>(self).value()(
            std::forward<Args>(args)...)))
    {
        return std::forward<Self>(self).value()(std::forward<Args>(args)...);
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn — universal mint pattern (CLAUDE.md §XXI) ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Token-mint flavor: no Ctx parameter.  Authority derives from the
// `Grants...` pack itself.  The single concept gate is IsAccepted;
// the substrate's ValidComposition fires inside `fn<Type, Grants...>`'s
// underlying_fn_t resolution if a §6.8 rule violation reaches it.
//
// Phase B's mint_fn is the SINGLE public construction surface;
// authors who want to bypass the gate must reach for
// `safety::fn::Fn<...>` directly (a documented exit-the-discipline
// pattern, not encouraged).

template <typename... Grants, typename Type>
    requires IsAccepted<Grants...>
[[nodiscard]] constexpr auto mint_fn(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
    -> fn<Type, Grants...>
{
    return fn<Type, Grants...>{std::move(v)};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// AllStrictPack on int produces fn<int, ...> whose underlying_fn_t IS
// safety::fn::Fn<int> (substrate defaults).
using AllStrictFn = fn<int,
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

static_assert(std::is_same_v<typename AllStrictFn::underlying_fn_t,
                             ::crucible::safety::fn::Fn<int>>,
    "fixy::fn<int, AllStrict-pack> must resolve to safety::fn::Fn<int> with "
    "substrate's shipping defaults.");

// EBO-zero invariant.
static_assert(sizeof(AllStrictFn) == sizeof(int),
    "fixy::fn must add zero bytes to the underlying Type; the 20-dim "
    "grade vector is type-level metadata only.");

// fn is `final` — no extension allowed (mirrors grant_base discipline).
static_assert(::crucible::safety::NotInherited<AllStrictFn>);

// IsAccepted gate is reachable via the wrapper's traits.
static_assert(IsAccepted<
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
>);

}  // namespace self_test

}  // namespace crucible::fixy
