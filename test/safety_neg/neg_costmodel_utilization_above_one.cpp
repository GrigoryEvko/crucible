// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidUtilization is a fixy::Refined over valid_utilization, the closed
// range [0, 1], and the checked mint is its only door.  This fixture gives
// the mint 1.5.  It catches a swapped numerator and denominator in a
// utilization formula.
//
// A utilization above one gives an effective rate above the hardware
// peak in evaluate_cost.  It also hides a kernel from the UNDERUTIL test
// there.
//
// The companion fixture neg_costmodel_utilization_negative gives the mint
// -0.5, below the range.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

int main() {
    constexpr crucible::ValidUtilization bad = ::fixy::mint_refined<crucible::valid_utilization>(1.5f);
    (void)bad;
    return 0;
}
