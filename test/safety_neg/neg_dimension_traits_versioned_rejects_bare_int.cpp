// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-3 fixture #2 of 2 for safety::DimensionTraits.h
// (#1094) — proves that the Tier-V concept gate `VersionedGrade<G>`
// rejects a bare scalar type (`int`) that has neither an
// `element_type` typedef nor a `compatible(a, b)` static method.
//
// The Tier-V composition law (consistency check at each par/seq
// site) requires the grade carrier to expose a binary
// compatibility predicate so the runtime can verify that two
// version values may compose without semantic drift (e.g., minor-
// version compatibility, schema-version equivalence).  A reviewer
// who weakens the gate down to `requires { typename G::element_type; }`
// would silently admit ANY type with an `element_type` (every
// std::vector, every std::array, every std::span) — none of which
// carry a compatibility predicate; admitting them strips the
// composition law's check at every Tier-V site.
//
// `int` is the canonical adversarial witness: as a bare scalar it
// has no nested typedefs and no static methods.  If the concept
// admits it, the gate is broken.
//
// Expected diagnostic: "constraints not satisfied" pointing at
// the VersionedGrade<G> concept evaluation.

#include <crucible/safety/DimensionTraits.h>

namespace neg = crucible::safety;

// Probe — a function template that consumes the VersionedGrade gate.
// Instantiating with `int` forces the compiler to evaluate the
// requires-clause against a type that exposes neither
// `int::element_type` (nested type lookup fails) nor
// `int::compatible(a, b)` (member lookup fails); substitution
// failure with structured diagnostic.
template <neg::VersionedGrade G>
constexpr bool consumes_versioned() noexcept {
    using E = typename G::element_type;
    return G::compatible(E{}, E{});
}

// Bridge fires: instantiation site demands VersionedGrade<int>;
// `int` lacks both required type-level features; compilation aborts.
static_assert(consumes_versioned<int>());

int main() { return 0; }
