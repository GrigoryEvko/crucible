// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an AllocClassLattice::At<TAG_B>::element_type
// to a function expecting AllocClassLattice::At<TAG_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors the per-At<T> mixing fixtures shipped for the five sister
// chain lattices.  A future refactor extracting a shared
// singleton_carrier<AllocClassTag> alias above At<T> would silently
// allow Stack-tier and Heap-tier values to interconvert at the
// lattice level, bypassing the no-malloc-on-hot-path discipline
// via Graded's compose / weaken paths.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/AllocClassLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    AllocClassLattice::At<AllocClassTag::Stack>::element_type stack_elt{};
    AllocClassLattice::At<AllocClassTag::Heap>::element_type  heap_elt{};

    // Should FAIL: At<Stack>::leq expects two At<Stack>::element_type
    // arguments; heap_elt is At<Heap>::element_type.
    return static_cast<int>(
        AllocClassLattice::At<AllocClassTag::Stack>::leq(stack_elt, heap_elt));
}
