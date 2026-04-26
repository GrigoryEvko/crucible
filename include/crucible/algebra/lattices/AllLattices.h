#pragma once

// ── crucible::algebra::lattices — umbrella ──────────────────────────
//
// Aggregates every concrete lattice instantiation under
// algebra/lattices/.  Pulled in by algebra/Algebra.h; individual
// per-lattice headers can be included directly to minimize per-TU
// compile cost.
//
// Each per-lattice header MUST:
//   - include <crucible/algebra/Lattice.h>
//   - publish a `name()` for diagnostic emission
//   - emit a self-test block invoking the `verify_*` helpers from
//     Lattice.h at every representative element witness
//   - be free of allocations and runtime state (consteval-only)
//
// The forward declarations below let dependent code (MIGRATE-1..11
// alias headers, future ProductLattice instantiations) name a lattice
// before its full header lands — useful during the staggered rollout
// of ALGEBRA-4..15.  Forward-declared lattices DO NOT yet satisfy the
// Lattice concept; instantiating Graded<> over them fails until the
// per-lattice header is included.
//
// Lattice                     | Task    | Used by
// ----------------------------+---------+---------------------------------
// QttSemiring                 | #449    | Linear<T>, Permission<Tag>
// BoolLattice<Pred>           | #450    | Refined<Pred, T>
// ConfLattice                 | #451    | Secret<T>
// TrustLattice<Source>        | #452    | Tagged<T, Source>
// FractionalLattice           | #453    | SharedPermission<Tag>
// MonotoneLattice<T, Cmp>     | #454    | Monotonic<T, Cmp>
// SeqPrefixLattice            | #455    | AppendOnly<T>
// StalenessSemiring           | #456    | Stale<T>             (NEW)
// LatencyBudget /             | #457    | Budgeted<budget, T>  (NEW)
//   EnergyBudget /            |         |
//   BitsBudget /              |         |
//   PeakBytes                 |         |
// HappensBeforeLattice        | #458    | TimeOrdered<T, Clk>  (NEW)
// LifetimeLattice /           | #459    | SessionOpaqueState,
//   ConsistencyLattice /      |         | BatchPolicy axes,
//   ToleranceLattice          |         | precision-budget calibrator
// ProductLattice<L₁, L₂, ...> | #460    | every multi-grade composition

// ── Shipped lattices ────────────────────────────────────────────────
#include <crucible/algebra/lattices/BoolLattice.h>  // ALGEBRA-5 (#450) — shipped
#include <crucible/algebra/lattices/ConfLattice.h>  // ALGEBRA-6 (#451) — shipped
#include <crucible/algebra/lattices/QttSemiring.h>  // ALGEBRA-4 (#449) — shipped

namespace crucible::algebra::lattices {

// ── Forward declarations (full definitions land per ALGEBRA-7..15) ──

// BoolLattice<Pred> — already included above (ALGEBRA-5 shipped).

// ConfLattice — already included above (ALGEBRA-6 shipped).

// Trust lattice parameterized over a tag-namespace family
// (source::*, trust::*, access::*, version::*).
template <typename Source> struct TrustLattice;

// ℚ[0,1] semiring for fractional CSL permissions.
struct FractionalLattice;

// Per-comparison-functor monotone lattice.
template <typename T, typename Cmp> struct MonotoneLattice;

// Sequence prefix lattice for AppendOnly streams.
template <typename Element> struct SeqPrefixLattice;

// ℕ ∪ ∞ with min-plus algebra (staleness, latency).
struct StalenessSemiring;

// Budget lattices — magnitudes with ≤ ordering.
struct LatencyBudget;
struct EnergyBudget;
struct BitsBudget;
struct PeakBytes;

// Phantom Before/After ordering lattice.
template <typename Clock> struct HappensBeforeLattice;

// Lifetime lattice (PER_REQUEST < PER_PROGRAM < PER_FLEET).
struct LifetimeLattice;

// Consistency lattice (Strong < BoundedStaleness < CausalPrefix <
// ReadYourWrites < Eventual).
struct ConsistencyLattice;

// Tolerance lattice (relative/absolute error magnitudes).
struct ToleranceLattice;

// Componentwise product over N lattices.
template <typename... Ls> struct ProductLattice;

}  // namespace crucible::algebra::lattices
