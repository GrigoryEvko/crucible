// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidWarpSize is a fixy::Refined over valid_warp_size, which requires a
// power of two no larger than 128, and the checked mint is its only door.
// This fixture gives the mint 33, which is not a power of two.  It catches
// a caller that computes the width as a nominal width plus one.
//
// A warp of 33 lanes matches no dispatch width of any hardware.  The
// thread counts of max_threads_per_sm, wave_efficiency and sm_occupancy
// would then describe no real part.
//
// The companion fixture neg_costmodel_warp_size_too_large gives the mint
// 256, a power of two above the ceiling.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidWarpSize bad = ::fixy::mint_refined<crucible::valid_warp_size>(uint16_t{33});
    (void)bad;
    return 0;
}
