// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidUtilization is a fixy::Refined over valid_utilization, the closed
// range [0, 1], and the checked mint is its only door.  This fixture gives
// the mint -0.5.  It catches a sign flip, or a signed difference that goes
// below zero before the conversion to float.
//
// A negative utilization gives a negative compute time in evaluate_cost,
// and the bottleneck classification then reads a corrupt value as a
// plausible one.
//
// The companion fixture neg_costmodel_utilization_above_one gives the
// mint 1.5, above the range.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

int main() {
    constexpr crucible::ValidUtilization bad = ::fixy::mint_refined<crucible::valid_utilization>(-0.5f);
    (void)bad;
    return 0;
}
