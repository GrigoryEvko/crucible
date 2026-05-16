#pragma once

// ── crucible::fixy — Value.h (FIXY-G2) ─────────────────────────────────
//
// Value-grade alignment.  `value_t<F>` projects a fixy::fn<T, Grants...>
// to a substrate-typed return carrier whose `raw()` accessor gives the
// underlying T.  When the binding is grade-equivalent to the all-strict
// default for substrate-EBO-collapsible axes, `value_t<F>` IS just T
// (zero overhead — the substrate stack collapses entirely under EBO).
//
// **Scope.**  This Gap-2 implementation ships the metafunction surface
// and the EBO-alignment concept (`is_value_aligned`).  Full canonical-
// order substrate stack composition (HotPath ⊃ DetSafe ⊃ NumericalTier
// ⊃ Vendor ⊃ ... — see CLAUDE.md §XVI) lands incrementally as the
// downstream axes' wrappers ship their factory APIs; today, `value_t`
// returns the type-axis-resolved `F::type_t` and the alignment check
// holds against it.  Wrapping-when-needed is added per-axis without
// changing `value_t<F>`'s grep-target signature.
//
// **Surface.**
//
//   fixy::value_t<F>            — substrate carrier for F's return type.
//   fixy::is_value_aligned<F>   — concept: sizeof(raw) == sizeof(T).
//   fn<T, Grants...>::call(args...) -> value_t<F>
//                                — auto-wraps the bare F::type_t().
//                                  (Current shape returns F::type_t
//                                  directly; new wrappers slot in
//                                  without API change.)
//
// **EBO discipline.**  `sizeof(value_t<F>) == sizeof(F::type_t)` must
// hold for every shipped binding under the canonical wrapper-nesting
// order from CLAUDE.md §XVI.  `is_value_aligned<F>` is the runtime
// check the consumer can `requires` on to guarantee zero overhead.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — return path threads through F::value_; no new fields.
//   TypeSafe   — type_t carries the substrate's typed-wrapper identity.
//   NullSafe   — concept-gated; non-fn F is a compile error.
//   MemSafe    — zero allocation; substrate wrappers are EBO-collapsible.
//   BorrowSafe — call(args) is move-only when F's underlying is move-only.
//   ThreadSafe — call() inherits F's thread-safety semantic.
//   LeakSafe   — no resources beyond F.
//   DetSafe    — pure type-level projection; same F → same value_t.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — value-grade alignment
//   CLAUDE.md §XVI                        — canonical wrapper-nesting order
//   algebra/Graded.h                      — substrate algebraic skeleton

#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ─── value_t<F> ────────────────────────────────────────────────────
//
// Projects F to a substrate-typed return carrier.  Current shape is
// the type-axis-resolved F::type_t — the underlying substrate type
// already carries every relaxed-axis discipline (Refined / Tagged /
// Linear / ...) the resolver inserts.  Future iterations attach the
// outer-axis wrappers (HotPath / DetSafe / NumericalTier / Vendor /
// ResidencyHeat / CipherTier / AllocClass / Wait / MemOrder /
// Progress / Stale) per the canonical-nesting order; the metafunction
// signature stays stable.

namespace detail {

template <typename F>
struct value_t_impl;

template <typename T, typename... Grants>
struct value_t_impl<::crucible::fixy::fn<T, Grants...>> {
    using type = typename ::crucible::fixy::fn<T, Grants...>::type_t;
};

}  // namespace detail

template <typename F>
    requires IsFixyFn<F>
using value_t = typename detail::value_t_impl<std::remove_cvref_t<F>>::type;

// ─── is_value_aligned<F> concept ───────────────────────────────────
//
// True when `sizeof(value_t<F>) == sizeof(F::type_t)` — the substrate
// stack collapsed to byte-equivalent under EBO.  Authors can
// `requires is_value_aligned<F>` on hot-path call sites to guarantee
// zero overhead at the type boundary.

template <typename F>
concept is_value_aligned =
    IsFixyFn<F> &&
    (sizeof(value_t<F>) == sizeof(typename std::remove_cvref_t<F>::type_t));

// ─── Self-tests ────────────────────────────────────────────────────

namespace self_test {

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

static_assert(is_value_aligned<AllStrictFn>,
    "all-strict binding must project to byte-equivalent carrier; the "
    "substrate stack must EBO-collapse under -O3.  A regression here "
    "means a new outer wrapper introduced non-collapsible state.");

static_assert(std::is_same_v<value_t<AllStrictFn>, AllStrictFn::type_t>,
    "Phase-G2 value_t identity: today's implementation returns F::type_t "
    "directly; future iterations may insert outer wrappers, in which "
    "case THIS test becomes the canary that an axis is now wrapped.");

}  // namespace self_test

}  // namespace crucible::fixy
