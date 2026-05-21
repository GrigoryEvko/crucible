#pragma once
// fixy::wrap::Refined granular surface.
//
// Foundational headers (Types.h and peers) need only the Refined family
// — Refined<Pred, T> plus its predicates and combinator — NOT the full
// fixy/Wrap.h umbrella.  The umbrella re-exports every safety wrapper,
// including OwnedRegion → Arena → LifetimeLattice; dragging that into the
// near-universal Types.h regressed the SessionPersistence.h dep-edge
// guard (session_persistence_include_audit, fixy-A2-014): Types.h is
// transitively pulled by SessionPersistence.h, so Types.h → fixy/Wrap.h →
// OwnedRegion.h → Arena.h re-introduced the heavy transitive set the
// guard exists to keep out.
//
// This granular header surfaces the Refined family through fixy::wrap::
// at zero Arena cost — safety/Refined.h + safety/RefinedAlgebra.h pull
// only Graded + BoolLattice + Linear (verified Arena-free).  fixy/Wrap.h
// includes this header so the umbrella still surfaces the full family
// (single source of truth); light consumers include THIS header directly.

#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>   // all_of predicate combinator

namespace crucible::fixy::wrap {

// Refined<Pred, T> — predicate-checked at construction.
using ::crucible::safety::Refined;
// Named refinement aliases — load-bearing per CLAUDE.md §XVI.
using ::crucible::safety::NonNull;
using ::crucible::safety::Positive;
using ::crucible::safety::NonNegative;
using ::crucible::safety::PowerOfTwo;
// Refined composition with Linear (both orderings).
using ::crucible::safety::LinearRefined;
using ::crucible::safety::RefinedLinear;
// Common stateless predicates — usable as Refined NTTP.
using ::crucible::safety::positive;
using ::crucible::safety::non_negative;
using ::crucible::safety::non_zero;
using ::crucible::safety::non_null;
using ::crucible::safety::power_of_two;
using ::crucible::safety::non_empty;
// Predicate combinator (RefinedAlgebra.h) — conjunction of predicates,
// `all_of<power_of_two, bounded_above<128>>` etc.  FIXY-U-096s.
using ::crucible::safety::all_of;
// Parameterised predicate templates.
using ::crucible::safety::Aligned;
using ::crucible::safety::InRange;
using ::crucible::safety::BoundedAbove;
using ::crucible::safety::LengthGe;
using ::crucible::safety::aligned;
using ::crucible::safety::in_range;
using ::crucible::safety::bounded_above;
using ::crucible::safety::length_ge;
// Cross-predicate implication trait — used by SessionPayloadSubsort.
using ::crucible::safety::predicate_implies;
using ::crucible::safety::implies_v;

}  // namespace crucible::fixy::wrap
