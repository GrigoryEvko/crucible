// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a CrashLattice::At<CLASS_B>::element_type
// to a function expecting CrashLattice::At<CLASS_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors the per-At<T> mixing fixtures shipped for the nine
// sister chain/partial-order lattices.  A future refactor
// extracting a shared singleton_carrier<CrashClass> alias above
// At<T> would silently allow NoThrow-class and Abort-class values
// to interconvert at the lattice level, bypassing the failure-
// mode discipline via Graded's compose / weaken paths.
//
// THE LOAD-BEARING CASE: an Abort-tier value masquerading as
// NoThrow at the LATTICE level (below the wrapper) would defeat
// the relax<>() requires-clause fence — the wrapper's relax<>()
// calls leq() at the lattice level, and if At<NoThrow> and
// At<Abort> shared an element_type, the lattice's leq would treat
// them as comparable and admit the conversion.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/CrashLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    CrashLattice::At<CrashClass::NoThrow>::element_type nothrow_elt{};
    CrashLattice::At<CrashClass::Abort>::element_type   abort_elt{};

    // Should FAIL: At<NoThrow>::leq expects two At<NoThrow>::element_type
    // arguments; abort_elt is At<Abort>::element_type.
    return static_cast<int>(
        CrashLattice::At<CrashClass::NoThrow>::leq(nothrow_elt, abort_elt));
}
