// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-265 MemoryScopeLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// MemoryScopeLattice::leq's signature requires two MemoryScope values.
// Passing a BarrierStrength value (a SIBLING Agent 11 lattice, also uint8_t
// underlying) as the second argument is a strong-enum type-mismatch —
// `enum class` admits NO implicit cross-enum conversion.  This pins the
// TypeSafe disjointness the V-267 ScopedFence gate depends on: a memory-
// visibility scope must never be silently compared against a fence-strength
// tier (the two distinct Agent 11 / WMEM axes that COMPOSE via wrapper-
// nesting, never via a single lattice op).
//
// Mirrors test/safety_neg/neg_barrier_strength_cross_lattice_mixing.cpp.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    MemoryScope     scope_val   = MemoryScope::Gpu;
    BarrierStrength barrier_val = BarrierStrength::SeqCst;

    // Should FAIL: MemoryScopeLattice::leq requires two MemoryScope values;
    // passing a BarrierStrength as the second argument is a type mismatch
    // (no cross-enum implicit conversion).
    return static_cast<int>(
        MemoryScopeLattice::leq(scope_val, barrier_val));
}
