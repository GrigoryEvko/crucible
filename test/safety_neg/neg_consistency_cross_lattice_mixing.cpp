// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a Consistency value to ConsistencyLattice::leq
// alongside a Lifetime value (or vice versa).
//
// Consistency and Lifetime are STRUCTURALLY DIFFERENT enum classes
// — they share the underlying type (uint8_t) but the two enum types
// are not implicitly convertible.  A function expecting
// `Consistency` rejects a `Lifetime` argument at the call site
// without any framework-owned static_assert; the type system catches
// the cross-lattice mixing structurally.
//
// This test pins the per-lattice element_type discipline: each
// chain lattice (Lifetime / Consistency / Tolerance) carries its
// OWN strong enum, and BatchPolicy<Axis, Level> code that picks
// per-axis tiers cannot accidentally swap a tolerance budget for
// a consistency level (or any cross-mixing among the three).
//
// A future refactor that (e.g.) collapsed the per-lattice enums
// into a single shared `Tier : uint8_t` for "convenience" would
// silently allow a Lifetime::PER_FLEET to flow where a
// Consistency::STRONG was expected — semantically catastrophic
// even though both are uint8_t value=4.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.  Acceptable: enum-class type identity
// is a structural C++ property the framework can't make more
// explicit without subverting `enum class`.

#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/LifetimeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    Consistency consistency_val = Consistency::EVENTUAL;
    Lifetime    lifetime_val    = Lifetime::PER_FLEET;

    // Should FAIL: ConsistencyLattice::leq's signature requires two
    // Consistency values; passing a Lifetime as the second argument
    // is a type-mismatch (Lifetime is NOT convertible to Consistency
    // — both are `enum class : uint8_t` so no implicit narrowing
    // applies).
    return static_cast<int>(
        ConsistencyLattice::leq(consistency_val, lifetime_val));
}
