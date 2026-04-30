#pragma once

// ── crucible::safety::can_fuse_v / IsFusable ────────────────────────
//
// FOUND-F06.  Structural-composability predicate: can two function
// callables `Fn1` and `Fn2` be fused into a single combined pass?
//
// In Crucible's L9 vocabulary (CLAUDE.md), kernel fusion combines
// adjacent producer-consumer ops into one kernel that keeps the
// intermediate result in registers / shared memory rather than
// round-tripping through HBM.  This header ships the FOUNDATIONAL
// predicate; the actual fused-callable generation lands in F07
// (`fuse<Fn1, Fn2>()` lambda) which gates on `can_fuse_v`.
//
// ── V1 conditions (structural; conservative on purpose) ───────────
//
// `can_fuse_v<Fn1, Fn2>` is true iff ALL of the following hold:
//
//   1. `is_pure_function_v<Fn1>` AND `is_pure_function_v<Fn2>`
//      Both functions have an empty inferred Met(X) effect row
//      (no cap-tags / context types in their parameter lists).
//      Fusion REORDERS effects in observable time — a non-pure
//      callee makes that reordering observable, so V1 conservatively
//      bans it.  Future tiers may relax this for commuting effects.
//
//   2. `is_noexcept_v<Fn1>` AND `is_noexcept_v<Fn2>`
//      Throwing under fusion changes observable behaviour: an
//      exception thrown by Fn1 in the un-fused form is observed
//      AFTER its full output is computed; under fusion the
//      partial-output is in register state and cannot be observed
//      cleanly.  The project compiles `-fno-exceptions` so this
//      check is structural-only, but the explicit guard documents
//      intent for any future preset that re-enables exceptions.
//
//   3. `arity_v<Fn2> == 1`
//      Fusion is the unary single-producer-single-consumer chain.
//      Multi-arity Fn2 is the multi-producer fan-in shape, which
//      F07's V1 lambda generator does not handle (the V1 fused
//      callable is `[](auto x) noexcept { return Fn2(Fn1(x)); }` —
//      a strictly chained call).
//
//   4. `!std::is_same_v<return_type_t<Fn1>, void>`
//      Fn1 must produce a value to flow into Fn2.  A void-returning
//      Fn1 with a non-void Fn2 input is structurally ill-formed for
//      fusion.
//
//   5. `std::is_same_v<return_type_t<Fn1>, remove_cvref_t<param_type_t<Fn2, 0>>>`
//      The composability check.  Fn1's return type, after dropping
//      reference / const-volatile qualifiers on Fn2's parameter,
//      must match exactly.  No implicit conversions admitted in
//      V1 — they introduce a hidden cost that defeats fusion's
//      "intermediate-in-registers" promise.
//
// ── What V1 deliberately does NOT check ───────────────────────────
//
// (a) Side effects via global state.  A function pointer's signature
//     does not surface global writes; the inferred row only counts
//     cap-tag parameters.  Reviewers and the Z3 verify preset
//     are responsible for global-state hygiene.  This predicate is
//     structural, not semantic.
//
// (b) Cost / register pressure.  V1 only answers "is fusion legal?",
//     not "is fusion profitable?".  The latter is the Augur layer's
//     job (cost model + Mimic kernel selection per CLAUDE.md L15).
//
// (c) Reordering of cross-iteration dependencies.  V1 fuses one
//     pair `(Fn1, Fn2)`; loop-level fusion that reorders dependencies
//     is the F08 / Augur problem, not this predicate.
//
// ── Public surface ────────────────────────────────────────────────
//
//   can_fuse_v<Fn1, Fn2>   bool variable template; true iff fusable.
//   IsFusable<Fn1, Fn2>    concept form for use in requires-clauses.
//
// ── Usage ────────────────────────────────────────────────────────
//
//   inline int g(int x) noexcept { return x * 2; }
//   inline int h(int x) noexcept { return x + 1; }
//
//   static_assert(::crucible::safety::can_fuse_v<&g, &h>);
//
//   // F07 (when shipped):
//   //   constexpr auto fused = ::crucible::safety::fuse<&g, &h>();
//   //   int y = fused(7);  // == h(g(7)) == 15

#include <crucible/safety/InferredRow.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety {

namespace detail {

// Helper: is the `I`-th parameter of `Fn` (after cv-ref strip) the
// same as `T`?  Folded into the can_fuse predicate to keep the
// composability comparison local.
template <auto Fn, std::size_t I, typename T>
inline constexpr bool param_matches_v =
    std::is_same_v<
        std::remove_cvref_t<extract::param_type_t<Fn, I>>,
        T>;

// Helper: structural shape of Fn — `is_pure && is_noexcept` and
// (for the consumer side) arity == 1.  Centralized so the public
// predicate's expression stays readable.
template <auto Fn>
inline constexpr bool fusable_producer_shape_v =
    extract::is_pure_function_v<Fn> &&
    extract::is_noexcept_v<Fn> &&
    !std::is_same_v<extract::return_type_t<Fn>, void>;

template <auto Fn>
inline constexpr bool fusable_consumer_shape_v =
    extract::is_pure_function_v<Fn> &&
    extract::is_noexcept_v<Fn> &&
    extract::arity_v<Fn> == 1;

}  // namespace detail

// ─── can_fuse_v<Fn1, Fn2> ──────────────────────────────────────────
//
// Implementation note: the param-match check must NOT instantiate
// `param_type_t<Fn2, 0>` when Fn2 is nullary, because
// SignatureTraits's `requires (I < arity)` clause produces a HARD
// compile error for out-of-range I (intentionally — it's a guard,
// not a SFINAE-friendly trait).  We short-circuit via `if constexpr`
// so the param-match step is only reached after arity is verified.

namespace detail {

template <auto Fn1, auto Fn2>
[[nodiscard]] consteval bool can_fuse_impl() noexcept {
    if constexpr (!fusable_producer_shape_v<Fn1>) {
        return false;
    } else if constexpr (!fusable_consumer_shape_v<Fn2>) {
        return false;
    } else {
        return param_matches_v<Fn2, 0, extract::return_type_t<Fn1>>;
    }
}

}  // namespace detail

template <auto Fn1, auto Fn2>
inline constexpr bool can_fuse_v = detail::can_fuse_impl<Fn1, Fn2>();

// ─── IsFusable<Fn1, Fn2> concept ─────────────────────────────────

template <auto Fn1, auto Fn2>
concept IsFusable = can_fuse_v<Fn1, Fn2>;

// ═════════════════════════════════════════════════════════════════════
// ── fuse<Fn1, Fn2>() — fused callable generator (FOUND-F07) ────────
// ═════════════════════════════════════════════════════════════════════
//
// Returns a stateless callable equivalent to `Fn2(Fn1(x))` — the
// composed pass with the intermediate held in a register across the
// composition (no heap, no temporary array).  Gated on the F06
// `IsFusable` concept so substitution fails loudly when the pair
// is not composable.
//
// V1 implementation: a lambda that captures nothing and forwards its
// argument by value into Fn1, then forwards the result by value into
// Fn2.  Result type deduced via `decltype(Fn2(Fn1(x)))`.
//
// Why a lambda (not a free function template):
//
//   * The dispatcher consumes the FUSED CALLABLE as a plain value-
//     typed object, not a function pointer — passing a lambda matches
//     the consumer interface (std::function_ref or auto&&).
//   * Stateless lambdas (no capture) decay to function pointers when
//     the consumer wants one; no expressivity loss.
//   * Inlining: every modern optimizer fully inlines stateless
//     lambdas, so the fused form compiles identically to the manual
//     `[](auto x) { return Fn2(Fn1(x)); }` written by hand.
//
// Why not return std::function<...>:
//
//   * std::function heap-allocates on capture (we ban it on hot path
//     per CLAUDE.md §IV opt-out).  The fused lambda is a stateless
//     closure — std::function would type-erase + indirect-call for no
//     benefit.
//
// Usage:
//
//   inline int g(int x) noexcept { return x * 2; }
//   inline int h(int x) noexcept { return x + 1; }
//
//   constexpr auto fused = ::crucible::safety::fuse<&g, &h>();
//   int y = fused(7);    // == h(g(7)) == 15
//
//   // Or one-shot at the call site:
//   int z = ::crucible::safety::fuse<&g, &h>()(7);
//
// Empirical validation:
//
//   `bench/bench_fusion.cpp` (FOUND-F08) benches three (manual,
//   fused) pairs of differing arithmetic depth and reports the
//   per-pair cycle delta.  Read its banner for the dev-hardware
//   findings; semantic equivalence is enforced by the F06 +
//   self-test asserts above, while the bench documents the
//   regime where compiler scheduling is identical (Pair 0 ICF)
//   vs where it diverges by a single cycle (Pair 2).

template <auto Fn1, auto Fn2>
    requires IsFusable<Fn1, Fn2>
[[nodiscard]] constexpr auto fuse() noexcept {
    // Stateless lambda — captures nothing.  Argument forwarded by
    // value into Fn1; the intermediate is a temporary held in the
    // call frame's register; Fn2 consumes it.
    //
    // The lambda's call operator is implicitly noexcept because both
    // Fn1 and Fn2 are noexcept (gated by the F06 predicate).
    return [](auto x) noexcept(noexcept(Fn2(Fn1(x))))
        -> decltype(Fn2(Fn1(x)))
    {
        return Fn2(Fn1(x));
    };
}

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::fuse_self_test {

inline int p_double(int x)    noexcept { return x * 2; }
inline int p_inc   (int x)    noexcept { return x + 1; }
inline double p_to_double(int x) noexcept { return static_cast<double>(x); }
inline int    p_to_int   (double x) noexcept { return static_cast<int>(x); }

// Identity-shape: int → int composed with int → int.
constexpr auto fused_double_then_inc = fuse<&p_double, &p_inc>();
static_assert(fused_double_then_inc(7)  == 15);  // (7*2)+1
static_assert(fused_double_then_inc(0)  == 1);
static_assert(fused_double_then_inc(-3) == -5);  // (-3*2)+1 == -5

// Type-changing: int → double → int.
constexpr auto fused_promote = fuse<&p_to_double, &p_to_int>();
static_assert(fused_promote(42) == 42);

// noexcept propagation.
static_assert(noexcept(fused_double_then_inc(0)));
static_assert(noexcept(fused_promote(0)));

// Result type deduction.
static_assert(std::is_same_v<decltype(fused_double_then_inc(0)), int>);
static_assert(std::is_same_v<decltype(fused_promote(0)), int>);

}  // namespace detail::fuse_self_test

// ── Self-test (can_fuse_v cases — original F06 self-test) ─────────

namespace detail::fusion_self_test {

inline int   producer_int_to_int(int x) noexcept { return x * 2; }
inline int   consumer_int_to_int(int x) noexcept { return x + 1; }
inline double producer_int_to_double(int x) noexcept { return static_cast<double>(x) * 1.5; }
inline int   consumer_double_to_int(double x) noexcept {
    return static_cast<int>(x);
}

// Positive: matching chain.
static_assert(can_fuse_v<&producer_int_to_int,    &consumer_int_to_int>);
static_assert(can_fuse_v<&producer_int_to_double, &consumer_double_to_int>);
static_assert(IsFusable<&producer_int_to_int,    &consumer_int_to_int>);

// Negative: type mismatch (int → int chained with double-consumer).
static_assert(!can_fuse_v<&producer_int_to_int, &consumer_double_to_int>);

// Negative: void-returning producer (no value to flow).
inline void producer_void(int) noexcept {}
static_assert(!can_fuse_v<&producer_void, &consumer_int_to_int>);

// Negative: nullary consumer (V1 requires unary Fn2).
inline int consumer_nullary() noexcept { return 0; }
static_assert(!can_fuse_v<&producer_int_to_int, &consumer_nullary>);

// Negative: binary consumer (V1 requires unary Fn2).
inline int consumer_binary(int, int) noexcept { return 0; }
static_assert(!can_fuse_v<&producer_int_to_int, &consumer_binary>);

}  // namespace detail::fusion_self_test

}  // namespace crucible::safety
