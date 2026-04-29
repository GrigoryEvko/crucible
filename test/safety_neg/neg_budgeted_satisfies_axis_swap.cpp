// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing PeakBytes where BitsBudget is expected
// at the Budgeted<T>::satisfies(BitsBudget, PeakBytes) admission
// gate.
//
// COMPANION TO neg_budgeted_axis_swap (which fences the constructor
// signature).  The satisfies() admission gate is the production
// dispatcher's per-call check:
//
//   if (!result.satisfies(BitsBudget{8192}, PeakBytes{1u<<20}))
//       return reject_oversize_value();
//
// If the maintainer flipped the axes —
//
//   if (!result.satisfies(peak_threshold, bits_threshold))   // ← flipped
//
// — the bug would compile silently if PeakBytes were a uint64_t
// alias of BitsBudget.  Because they are STRUCTURALLY DISTINCT
// strong-typed newtypes, the flip is a compile error.
//
// Pins the same TYPE-FENCED-AXIS discipline as the constructor
// fixture, but at the runtime-admission-gate surface.  Both must
// hold; either one passing in isolation is insufficient.
//
// [GCC-WRAPPER-TEXT] — satisfies parameter-type rejection.

#include <crucible/safety/Budgeted.h>

using namespace crucible::safety;

int main() {
    Budgeted<int> result{42, BitsBudget{1024}, PeakBytes{4096}};

    BitsBudget bits_threshold{8192};
    PeakBytes  peak_threshold{1u << 20};

    // Should FAIL: satisfies(BitsBudget, PeakBytes) requires axes
    // in declared order; passing (PeakBytes, BitsBudget) is a
    // type mismatch on both arguments.
    return static_cast<int>(
        result.satisfies(peak_threshold, bits_threshold));
}
