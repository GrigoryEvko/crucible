// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling combine_max() with EpochVersioned<U> argument
// when receiver is EpochVersioned<T> and T != U.
//
// Pins the composition-surface identity at the per-T level — same
// audit pattern as neg_budgeted_combine_max_cross_t.  A fan-in fold
// site that accidentally accepts a heterogeneous-T pair would
// silently slip through if the rejection wasn't pinned.
//
// [GCC-WRAPPER-TEXT] — combine_max parameter-type rejection.

#include <crucible/safety/EpochVersioned.h>

using namespace crucible::safety;

int main() {
    EpochVersioned<int>    int_value{42, Epoch{1}, Generation{1}};
    EpochVersioned<double> dbl_value{3.14, Epoch{2}, Generation{2}};

    // Should FAIL: EpochVersioned<int>::combine_max takes
    // EpochVersioned<int> const&; dbl_value is EpochVersioned<double>.
    auto bad = int_value.combine_max(dbl_value);
    return bad.peek();
}
