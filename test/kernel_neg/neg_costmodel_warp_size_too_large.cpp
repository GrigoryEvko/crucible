// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidWarpSize is a fixy::Refined over valid_warp_size, which requires a
// power of two no larger than 128, and the checked mint is its only door.
// This fixture gives the mint 256.  It is a power of two, but it is above
// the ceiling.  It catches a snapshot or a preset that claims a wider warp
// than any shipped part has.
//
// The widest shipped warp is 64 lanes, and 128 keeps one doubling of
// headroom.
//
// The companion fixture neg_costmodel_warp_size_not_power_of_two gives the
// mint 33, which is not a power of two.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidWarpSize bad = ::fixy::mint_refined<crucible::valid_warp_size>(uint16_t{256});
    (void)bad;
    return 0;
}
