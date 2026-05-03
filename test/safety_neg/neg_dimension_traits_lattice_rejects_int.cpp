// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-6 fixture (#1099) for safety::DimensionTraits.h's
// LatticeGrade concept (#1094).
//
// Why this matters: Tier-L dimensions (e.g., DimensionAxis::
// Representation) compose at par-sites via lattice join and at
// seq-sites via lattice meet (per fixy.md §24.1 + Crucible CLAUDE.md
// L0 §Tier).  A grade type used at a Tier-L site MUST satisfy
// algebra::Lattice — element_type, bottom(), top(), leq(a,b),
// join(a,b), meet(a,b).  Bare scalar types (int, uint8_t, ptrdiff_t)
// have NONE of these primitives at the type level — admitting them
// at a Tier-L site would silently fall through the join/meet
// operations to "do nothing", leaving the dimension's composition
// law unenforced.
//
// Without the LatticeGrade concept gate, a maintainer who writes
// `static_assert(LatticeGrade<int>)` thinking "int is a lattice
// under ≤" would pass the assertion and ship code that compiles
// but executes meaningless lattice ops downstream.  This fixture
// catches the mistake at the gate evaluation.
//
// Expected diagnostic: "constraints not satisfied" pointing at the
// LatticeGrade<int> evaluation, naming the algebra::Lattice<int>
// requirement that fails.

#include <crucible/safety/DimensionTraits.h>

namespace neg = crucible::safety;

// Bridge fires: instantiating consumes_lattice<int> trips the
// LatticeGrade constraint check; int has no element_type / bottom /
// top / leq / join / meet, so algebra::Lattice<int> is false.
template <neg::LatticeGrade G>
constexpr bool consumes_lattice() noexcept { return true; }

[[maybe_unused]] constexpr bool the_fixture = consumes_lattice<int>();

int main() { return 0; }
