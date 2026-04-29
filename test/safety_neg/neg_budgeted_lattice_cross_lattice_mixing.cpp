// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a PeakBytes value to BitsBudgetLattice::leq.
//
// Pins the structural disjointness of the two budget-axis lattices
// at the LATTICE substrate level, BELOW the wrapper.  Both
// BitsBudgetLattice and PeakBytesLattice carry uint64_t-backed
// elements, but their element_types are distinct strong-typed
// newtypes (BitsBudget and PeakBytes).  Cross-lattice mixing must
// be rejected.
//
// A future refactor that collapsed BitsBudget and PeakBytes into a
// shared `ResourceCount : uint64_t` for "convenience" would silently
// allow cross-axis mixing inside the ProductLattice<BitsBudgetLattice,
// PeakBytesLattice> substrate that Budgeted lives on top of.  The
// wrapper's compile-time fence (caught by the axis-swap fixture)
// would still hold, but the substrate-level fence would dissolve —
// and any direct ProductLattice consumer would silently accept
// flipped axes.
//
// [GCC-WRAPPER-TEXT] — leq parameter-type mismatch on the strong
// newtype carrier.

#include <crucible/algebra/lattices/BitsBudgetLattice.h>
#include <crucible/algebra/lattices/PeakBytesLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    BitsBudget bits{1024};
    PeakBytes  peak{4096};

    // Should FAIL: BitsBudgetLattice::leq's signature requires two
    // BitsBudget arguments; passing a PeakBytes as the second
    // argument is a type mismatch — even though both are uint64_t-
    // backed.
    return static_cast<int>(BitsBudgetLattice::leq(bits, peak));
}
