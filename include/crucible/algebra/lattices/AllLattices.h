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
//     Lattice.h at every representative element witness — exhaustive
//     when the carrier is finite (e.g. enums); spot-check at a
//     representative span when the carrier is infinite (e.g. ℕ, ℚ)
//   - emit a runtime smoke test (`inline void runtime_smoke_test()`)
//     exercising lattice ops AND Graded<...,L,T>::weaken / compose
//     with non-constant arguments, to catch the consteval-vs-constexpr
//     trap that pure static_assert tests miss
//   - be free of per-instance allocations in lattice operations
//     themselves (the lattice is stateless; the smoke harness may
//     use stack values)
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
// HappensBeforeLattice        | #458    | TimeOrdered<T, Clk>  (NEW)
// LifetimeLattice /           | #459    | SessionOpaqueState,
//   ConsistencyLattice /      |         | BatchPolicy axes,
//   ToleranceLattice          |         | precision-budget calibrator
// ProductLattice<L₁, L₂, ...> | #460    | every multi-grade composition
//
// NOTE: Latency-budget and energy-budget lattices were dropped from
// the roadmap.  Both quantities are not reliably measurable as
// type-level dimensions on the deployment surface (system-load-
// dependent latency, vendor-spotty / noisy RAPL-style energy
// counters), so committing them as graded-modality grades would push
// non-actionable contract obligations into the type system.  The
// related Budgeted<...> alias and its task entries (formerly #457
// ALGEBRA-12 Budget.h, #469 MIGRATE-9 Budgeted.h) are deleted.
// Bytes / sequence length / staleness / consistency / tolerance
// budgets remain — they ARE measurable at the type-level
// granularity Crucible needs.

// ── Shipped lattices ────────────────────────────────────────────────
#include <crucible/algebra/lattices/BoolLattice.h>       // ALGEBRA-5  (#450) — shipped
#include <crucible/algebra/lattices/ConfLattice.h>       // ALGEBRA-6  (#451) — shipped
#include <crucible/algebra/lattices/ConsistencyLattice.h>// ALGEBRA-14 (#459) — shipped (2/3)
#include <crucible/algebra/lattices/DetSafeLattice.h>    // FOUND-G13  — shipped
#include <crucible/algebra/lattices/FractionalLattice.h> // ALGEBRA-8  (#453) — shipped
#include <crucible/algebra/lattices/HappensBefore.h>     // ALGEBRA-13 (#458) — shipped
#include <crucible/algebra/lattices/HotPathLattice.h>    // FOUND-G18  — shipped
#include <crucible/algebra/lattices/LifetimeLattice.h>   // ALGEBRA-14 (#459) — shipped (1/3)
#include <crucible/algebra/lattices/MonotoneLattice.h>   // ALGEBRA-9  (#454) — shipped
#include <crucible/algebra/lattices/ProductLattice.h>    // ALGEBRA-15 (#460) — shipped
#include <crucible/algebra/lattices/QttSemiring.h>       // ALGEBRA-4  (#449) — shipped
#include <crucible/algebra/lattices/SeqPrefixLattice.h>  // ALGEBRA-10 (#455) — shipped
#include <crucible/algebra/lattices/StalenessSemiring.h> // ALGEBRA-11 (#456) — shipped
#include <crucible/algebra/lattices/ToleranceLattice.h>  // ALGEBRA-14 (#459) — shipped (3/3)
#include <crucible/algebra/lattices/TrustLattice.h>      // ALGEBRA-7  (#452) — shipped
#include <crucible/algebra/lattices/WaitLattice.h>       // FOUND-G23  — shipped

namespace crucible::algebra::lattices {

// ── Forward declarations (full definitions land per ALGEBRA-8..15) ──

// BoolLattice<Pred> — already included above (ALGEBRA-5 shipped).

// ConfLattice — already included above (ALGEBRA-6 shipped).

// TrustLattice<Source> — already included above (ALGEBRA-7 shipped).

// FractionalLattice — already included above (ALGEBRA-8 shipped).

// MonotoneLattice<T, Cmp> — already included above (ALGEBRA-9 shipped).

// SeqPrefixLattice<Element> — already included above (ALGEBRA-10 shipped).

// StalenessSemiring — already included above (ALGEBRA-11 shipped).

// HappensBeforeLattice — already included above (ALGEBRA-13 shipped).
// Vector-clock partial order over N participants with optional Tag
// for cross-protocol distinction.

// LifetimeLattice — already included above (ALGEBRA-14 part 1/3 shipped).
// Three-element chain: PER_REQUEST < PER_PROGRAM < PER_FLEET.

// ConsistencyLattice — already included above (ALGEBRA-14 part 2/3 shipped).
// Five-element chain: EVENTUAL < READ_YOUR_WRITES < CAUSAL_PREFIX <
// BOUNDED_STALENESS < STRONG.

// ToleranceLattice — already included above (ALGEBRA-14 part 3/3 shipped).
// Seven-tier chain over numeric-tolerance budgets:
// RELAXED < ULP_INT8 < ULP_FP8 < ULP_FP16 < ULP_FP32 < ULP_FP64 < BITEXACT.

// Componentwise product over N lattices.
template <typename... Ls> struct ProductLattice;

}  // namespace crucible::algebra::lattices
