// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing PeakBytes where BitsBudget is expected
// (or vice versa) at the Budgeted constructor.
//
// THE LOAD-BEARING REJECTION FOR THE PRODUCT-WRAPPER AXIS DISCIPLINE.
// Budgeted's constructor is `Budgeted(T, BitsBudget, PeakBytes)`.
// If a maintainer flipped the axes at a call site —
//   Budgeted(value, peak_bytes, bits_budget)   // ← flipped
// — and BOTH BitsBudget and PeakBytes were just `uint64_t`, the bug
// would compile silently and downstream gates checking
//   `result.bits().value <= max_bits`
// would actually compare against the peak-bytes counter, masking
// the real footprint axis.
//
// The strong-typed BitsBudget and PeakBytes newtypes (each a
// distinct C++ struct phantom-tagged with its purpose) make this
// flip a compile error: PeakBytes is NOT implicitly convertible to
// BitsBudget, even though both wrap uint64_t.
//
// [GCC-WRAPPER-TEXT] — constructor parameter type mismatch.

#include <crucible/safety/Budgeted.h>

using namespace crucible::safety;

int main() {
    BitsBudget bits{1024};
    PeakBytes  peak{4096};

    // Should FAIL: Budgeted<int>(int, BitsBudget, PeakBytes) cannot
    // accept (int, PeakBytes, BitsBudget) — axes are flipped.
    Budgeted<int> bad{42, peak, bits};
    return bad.peek();
}
