// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a DetSafeLattice::At<TIER_B>::element_type to a
// function expecting DetSafeLattice::At<TIER_A>::element_type.
//
// Symmetric to neg_consistency_at_cross_tier_mixing.cpp / neg_lifetime_
// at_cross_tier_mixing.cpp / neg_tolerance_at_cross_tier_mixing.cpp —
// pins the per-At<T> sub-lattice element_type identity at the
// DetSafeLattice level (the substrate underlying the 8th-axiom
// enforcer).  Each At<T> sub-lattice's element_type is a NESTED struct
// whose template identity depends on T; cross-tier mixing must be a
// type-mismatch.
//
// THIS IS THE LATTICE-LEVEL MIRROR OF neg_det_safe_relax_to_stronger.
// The latter pins per-tier identity at the WRAPPER surface (relax<>);
// THIS test pins it at the LATTICE substrate.  Both surfaces must
// reject cross-tier mixing or the Cipher write-fence is silently
// defeated:
//
//   - Refactor extracting a shared `singleton_carrier<DetSafeTier>`
//     template alias above DetSafeLattice::At<T> would make
//     At<Pure>::element_type and At<PhiloxRng>::element_type the same
//     C++ type.  Pure-tier and PhiloxRng-tier values would silently
//     interconvert at the lattice level.
//   - The wrapper-surface relax<>() rejection would still fire for
//     direct Pure→relax<Stronger> calls, but a lattice-level mix
//     could leak through Graded's compose / weaken paths that take
//     element_type values directly.
//
// Without this test, the 8th-axiom fence depends on a structural
// invariant (per-At<T> nested struct identity) that no other neg-
// compile test covers.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/DetSafeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    DetSafeLattice::At<DetSafeTier::Pure>::element_type      pure_elt{};
    DetSafeLattice::At<DetSafeTier::PhiloxRng>::element_type philox_elt{};

    // Should FAIL: At<Pure>::leq expects two At<Pure>::element_type
    // arguments; philox_elt is At<PhiloxRng>::element_type — different
    // template instantiation, different type, no implicit conversion.
    return static_cast<int>(
        DetSafeLattice::At<DetSafeTier::Pure>::leq(pure_elt, philox_elt));
}
