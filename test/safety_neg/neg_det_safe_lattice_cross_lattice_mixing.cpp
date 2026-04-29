// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a DetSafeTier value to ConsistencyLattice::leq
// (or any cross-lattice mixing across the four chain-lattice
// families: Tolerance / Consistency / Lifetime / DetSafe).
//
// Symmetric to neg_consistency_cross_lattice_mixing.cpp /
// neg_tolerance_cross_lattice_mixing.cpp / neg_lifetime_tolerance_
// cross_lattice_mixing.cpp.  Pins the structural disjointness of the
// four chain-lattice enums.  Each lattice (Tolerance / Consistency /
// Lifetime / DetSafe) carries its OWN strong scoped enum, and cross-
// lattice mixing must be rejected at type level — even though all
// four enum classes share the same underlying type (uint8_t).
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) DetSafeTier::Pure to flow where Consistency::STRONG was
// expected — semantically catastrophic even though both are uint8_t
// value=N.  This neg test would START passing (positive-compile)
// after such a regression.
//
// Specific concern for DetSafe: the 8th-axiom enforcer depends on
// DetSafeLattice::leq rejecting non-DetSafeTier values structurally.
// Without that rejection, code in the Cipher write-fence path could
// pass a Consistency or Tolerance value via an accidental
// std::to_underlying round-trip and silently bypass the determinism-
// safety check.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.  Acceptable: enum-class type identity is
// a structural C++ property the framework can't make more explicit
// without subverting `enum class`.

#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/DetSafeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    DetSafeTier det_safe_val   = DetSafeTier::Pure;
    Consistency consistency_val = Consistency::STRONG;

    // Should FAIL: ConsistencyLattice::leq's signature requires two
    // Consistency values; passing a DetSafeTier as the second
    // argument is a type-mismatch (DetSafeTier is NOT convertible to
    // Consistency — both are `enum class : uint8_t` so no implicit
    // narrowing applies).
    return static_cast<int>(
        ConsistencyLattice::leq(consistency_val, det_safe_val));
}
