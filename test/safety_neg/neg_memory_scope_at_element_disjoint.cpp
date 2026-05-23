// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-265 MemoryScopeLattice, mismatch class #2 of 2:
// At<S> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<S>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (MemoryScopeLattice vs BarrierStrengthLattice) are
// unrelated types with no conversion between them — even though both are
// empty and both EBO-collapse identically inside a Graded<> carrier.  This
// pins that a V-267 `Graded<Absolute, MemoryScopeLattice::At<S>, P>` grade
// cannot be silently initialized from a foreign-lattice grade: the
// federation-cache row_hash (V-267) keys on the wrapper's grade type, so a
// cross-lattice grade swap would corrupt the cache key.
//
// Distinct from neg_memory_scope_cross_lattice_mixing.cpp, which fails at a
// lattice OP argument; here the failure is a direct grade element_type
// assignment between two sibling lattices' singletons.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's no-viable-conversion
// rejection between two unrelated empty class types.

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    MemoryScopeLattice::At<MemoryScope::Gpu>::element_type scope_grade{};

    // Should FAIL: a BarrierStrength At<> grade cannot be initialized from a
    // MemoryScope At<> grade — they are unrelated empty class types.
    BarrierStrengthLattice::At<BarrierStrength::SeqCst>::element_type barrier_grade = scope_grade;

    return static_cast<int>(
        static_cast<BarrierStrength>(barrier_grade) == BarrierStrength::SeqCst);
}
