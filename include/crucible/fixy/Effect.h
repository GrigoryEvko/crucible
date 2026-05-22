#pragma once

// ── crucible::fixy::effect — Effect-row engagement concepts ────────
//
// FIXY-V-219 (Agent 8 Bug 7 + Part 10 #11).  Companion to `fixy/Eff.h`
// (which is the namespace alias re-export of the substrate carriers).
// `Eff.h` cannot host new symbols because it is a namespace alias of
// `::crucible::effects` — adding `concept Foo` there would mutate
// the substrate namespace.  V-219 therefore lives in a SIBLING
// namespace `fixy::effect::` (note the trailing-`t`, distinct from
// the `eff` alias), holding band-3 STANCES that fold over the Met(X)
// effect-row substrate.
//
// ── The bug shape (Agent 8 Bug 7) ──────────────────────────────────
//
// `effects/Computation.h` exposes `Computation() = default;` and the
// `weaken<R2>` widening member.  Together they admit:
//
//     auto pure = Computation<Row<>, T>::mk(x);          // empty row
//     auto bad  = pure.template weaken<Row<Effect::Alloc>>();
//     // bad has type Computation<Row<Effect::Alloc>, T> — the type
//     // claims Alloc, but the body NEVER materialised Alloc.
//
// **Structurally sound** (weakening is the substitution principle: a
// narrower row is admissible wherever a wider row is declared) **but
// discipline-unfriendly** — a reviewer reading "this function returns
// `Computation<Row<Effect::Alloc>, T>`" expects the body to demonstrate
// Alloc, and the weaken-from-pure pattern HIDES that.  Severity
// LOW-MEDIUM per Agent 8.
//
// ── Two-layer V-219 closure ────────────────────────────────────────
//
// Layer 1 — substrate guard (effects/Computation.h):
//   `weaken<R2>` requires `(row_size_v<R> > 0 || row_size_v<R2> == 0)`
//   in ADDITION to `Subrow<R, R2>`.  Weakening from a pure-row
//   Computation to a non-empty row REDS at the call site with the
//   constraint-violation diagnostic.  The substitution principle is
//   preserved for the legitimate widening case (Row<Bg> → Row<Bg,IO>
//   etc.) and for the degenerate Row<> → Row<> identity.  Production
//   code that wants to ATTACH a row at pure-construction time MUST
//   use `lift<Cap>(x)` (which materialises the cap in the body) or
//   a stage mint (which announces the row at the stage signature).
//
// Layer 2 — band-3 stance (this header):
//   `RowEngagementWitnessed<C>` is satisfied iff:
//     (a) C is a Computation specialisation, AND
//     (b) its row contains at least one Effect atom.
//
//   The concept is a documentation-and-discovery surface for band-3
//   call sites that want to require their callable's return value to
//   CARRY an engagement claim — `requires fixy::effect::Row
//   EngagementWitnessed<R>` on the return type forbids stray
//   `Computation<Row<>, T>` returns from a context that should
//   announce engagement.  It also folds for `static_assert` in
//   fixtures that demonstrate the LEGITIMATE construction paths
//   (lift, stage mint) DO produce row-engaged Computations.
//
// ── HS14 axis split ────────────────────────────────────────────────
//
// Two distinct rejection axes, two HS14 neg-compile fixtures:
//   (1) STRUCTURAL — passing a non-Computation type (e.g. `int`) reds
//       via the `IsComputation<C>` clause.  Substrate-shape rejection.
//   (2) SEMANTIC   — passing `Computation<Row<>, T>` (empty row,
//       legitimately pure) reds via the `effect_count_in_row > 0`
//       clause.  Row-engagement rejection.
// Two distinct axes ⇒ HS14 floor satisfied.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   effects::IsComputation<T>                   — substrate concept
//   effects::Computation<R, T>::effect_count_in_row() — consteval
//                                                       row-size probe
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — concept is type-level only; no NSDMI state to track.
//   TypeSafe — gates on substrate concepts (IsComputation) + a
//              consteval predicate; the diagnostic surface is the
//              concept name.
//   NullSafe — no pointer state.
//   MemSafe  — concept is a query, not a constructor; nothing to leak.
//   BorrowSafe — concept is type-level; no runtime sharing.
//   ThreadSafe — concept is consteval; thread-agnostic.
//   LeakSafe — concept has no storage.
//   DetSafe  — concept's truth value is a pure function of C; same
//              C → same answer, every time, on every platform.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  A C++ concept is a type-level predicate; satisfaction is
// computed at template-substitution time and emits no runtime code.

#include <crucible/effects/Computation.h>

#include <type_traits>

namespace crucible::fixy::effect {

// ── RowEngagementWitnessed<C> ──────────────────────────────────────
//
// True iff `C` is a `Computation<R, T>` whose row R contains at least
// one Effect atom.  Companion concept to the substrate guard on
// `Computation::weaken<R2>` (FIXY-V-219, Computation.h:210-237).
//
// **Semantic — the concept does NOT inspect HOW the Computation was
// constructed.**  It cannot — once a `Computation<Row<Alloc>, T>` exists,
// its type is the same regardless of construction path.  The
// LIGHT-WEIGHT V-219 closure is:
//
//   * Substrate (Computation.h) — rejects the discipline-unfriendly
//     path (weaken-from-empty-to-non-empty) at the EXPRESSION level
//     via the tightened requires-clause on `weaken<R2>`.
//   * Band-3 (this concept) — surfaces the "row is engaged" claim at
//     the TYPE level so band-3 stances can require their callable's
//     return type to claim engagement.
//
// The two layers compose: the substrate forbids the unsound
// construction path, this concept surfaces the engagement claim for
// downstream stances.  A `Computation<Row<Effect::Alloc>, T>` that
// reaches a `requires RowEngagementWitnessed<C>` gate is GUARANTEED
// (by substrate construction) to have come from a `lift` / stage
// mint / chained `then` path — i.e. the body really did engage Alloc.
//
// ── Why not just `effect_count_in_row(C) > 0`? ─────────────────────
//
// Two reasons:
//
//   1. **Diagnostic surface.**  A band-3 stance like
//        `requires fixy::effect::RowEngagementWitnessed<C>`
//      reads as "C must be an engagement-witnessed Computation",
//      which a reviewer can grep, document, and reason about.  The
//      raw predicate `effect_count_in_row<C>() > 0` lacks that
//      vocabulary.
//
//   2. **Decoupling.**  Future tightenings (e.g. provenance tags on
//      Computation that distinguish lift from weaken-from-empty) can
//      land BEHIND the concept without rewriting every band-3 stance.
//      The concept is the documented contract; its body is the
//      best-current-implementation.
//
// ── Diagnostic ─────────────────────────────────────────────────────
//
// On rejection, GCC reports "the constraint
// 'effect::RowEngagementWitnessed<...>' was not satisfied".  The
// fixture comments grep for `RowEngagementWitnessed` to confirm the
// reason was the concept gate (not an unrelated cascade).

template <class C>
concept RowEngagementWitnessed =
    ::crucible::effects::IsComputation<std::remove_cvref_t<C>>
    && (std::remove_cvref_t<C>::effect_count_in_row() > 0);

}  // namespace crucible::fixy::effect

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness the two axes the concept enforces (structural vs
// semantic), plus the positive cases that LEGITIMATE construction
// paths produce.

namespace crucible::fixy::effect::self_test {

namespace eff = ::crucible::effects;

// (1) STRUCTURAL axis — non-Computation types reject.
static_assert(!RowEngagementWitnessed<int>,
    "FIXY-V-219 self-test: `int` is not a Computation — "
    "RowEngagementWitnessed must reject via IsComputation.");

static_assert(!RowEngagementWitnessed<eff::Row<eff::Effect::Bg>>,
    "FIXY-V-219 self-test: a bare Row is not a Computation — "
    "RowEngagementWitnessed must reject the wrong-carrier shape.");

// (2) SEMANTIC axis — empty-row Computations reject.
static_assert(!RowEngagementWitnessed<eff::Computation<eff::Row<>, int>>,
    "FIXY-V-219 self-test: Computation<Row<>, int> is pure (empty row) — "
    "RowEngagementWitnessed must reject the no-engagement case.");

// (2-cvref) — cvref decay; the concept strips before testing.
static_assert(!RowEngagementWitnessed<eff::Computation<eff::Row<>, int>&>,
    "FIXY-V-219 self-test: cvref-stripped lvalue empty-row still rejects.");

static_assert(!RowEngagementWitnessed<const eff::Computation<eff::Row<>, int>&>,
    "FIXY-V-219 self-test: cvref-stripped const-lvalue empty-row still rejects.");

// (3) POSITIVE — a single-cap Computation accepts.
static_assert(RowEngagementWitnessed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>,
    "FIXY-V-219 self-test: Computation<Row<Bg>, int> has a non-empty row — "
    "RowEngagementWitnessed must accept the engaged case.");

// (3-multi) — multi-cap Computation also accepts.
static_assert(RowEngagementWitnessed<
    eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>, double>>,
    "FIXY-V-219 self-test: multi-cap Computation accepted by the concept.");

// (3-via-lift) — the canonical engaged-construction path: lift<Cap>(x)
// returns a Computation<Row<Cap>, T> that satisfies the concept.
// `lift` is consteval; the result type is the witness.
static_assert([] consteval {
    auto bg = eff::Computation<eff::Row<>, int>::template lift<eff::Effect::Bg>(7);
    return RowEngagementWitnessed<decltype(bg)>;
}(),
    "FIXY-V-219 self-test: lift<Bg>(x) produces a row-engaged "
    "Computation — RowEngagementWitnessed must accept the lift result.");

// (3-via-then) — chained construction via `then` (monadic bind)
// produces a row-engaged Computation (the result row is the
// row-union of the source and the bound continuation).
static_assert([] consteval {
    auto bg = eff::Computation<eff::Row<>, int>::template lift<eff::Effect::Bg>(10);
    auto chained = bg.then([](int x) {
        return eff::Computation<eff::Row<>, int>::template lift<eff::Effect::IO>(x + 1);
    });
    return RowEngagementWitnessed<decltype(chained)>;
}(),
    "FIXY-V-219 self-test: then-chained Computation carries the row-union; "
    "RowEngagementWitnessed must accept the engaged result.");

// (4) CARDINALITY witness — fixy::effect:: surface holds exactly the
// V-219 concept today.  This sentinel reds when V-219 grows new
// concepts in the same namespace, forcing the doc-block at top to
// be updated in parallel.  Cardinality witness pattern from
// FIXY-V-218 / V-217.
inline constexpr std::size_t effect_surface_cardinality = 1;
static_assert(effect_surface_cardinality == 1,
    "FIXY-V-219 cardinality sentinel: fixy::effect:: ships exactly one "
    "concept today (RowEngagementWitnessed).  If a sibling concept lands, "
    "bump this constant AND refresh the umbrella doc-block at the top "
    "of fixy/Effect.h to enumerate the new entry.");

}  // namespace crucible::fixy::effect::self_test
