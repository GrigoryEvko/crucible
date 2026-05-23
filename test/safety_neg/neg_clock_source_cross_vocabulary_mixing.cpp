// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-184 ClockSourceLattice, mismatch class #1 of 2:
// VALUE-VOCABULARY vs LATTICE-CARRIER confusion.
//
// `ClockSource` is the value-level VOCABULARY (nine named sources); the
// lattice CARRIER is the projected product 3-tuple
// `ClockSourceLattice::element_type`.  `ClockSourceLattice::leq` takes
// two element_type values — NOT two `ClockSource` enums.  A caller must
// run `clock_source_project(...)` FIRST.  Passing a raw `ClockSource`
// where the projected tuple is required is a type mismatch (no implicit
// conversion from the scoped enum to the product aggregate).  This pins
// the load-bearing distinction the whole composite rests on: the enum
// names a point, the projection FIXES it, and only the fixed tuple is
// ordered.
//
// Distinct from neg_clock_source_slot_axis_disjoint.cpp, which fails at
// a per-slot grade assignment; here the failure is at a lattice OP
// argument.
//
// Expected diagnostic: no match for / cannot convert / no matching
// function / invalid operands.

#include <crucible/algebra/lattices/ClockSourceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    // Should FAIL: leq requires two ClockSourceLattice::element_type
    // (projected tuples); a bare ClockSource enum does not convert.  The
    // caller must clock_source_project(...) each operand first.
    return static_cast<int>(
        ClockSourceLattice::leq(ClockSource::Boot, ClockSource::TscRaw));
}
