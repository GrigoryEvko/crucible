// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidRegsPerThread is a fixy::Refined over valid_regs_per_thread, a
// ceiling of 255, and the checked mint is its only door.  This fixture
// gives the mint 256, one above the ceiling.  It catches an off-by-one
// in a caller that computes the register count as a sum.
//
// 255 is the register ceiling on every shipped backend.  A larger count
// names no real hardware, and sm_occupancy would divide by it and give a
// thread count that no multiprocessor has.
//
// The companion fixture neg_costmodel_regs_per_thread_max_uint16 gives
// the mint the largest uint16_t, a wide miss.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidRegsPerThread bad = ::fixy::mint_refined<crucible::valid_regs_per_thread>(uint16_t{256});
    (void)bad;
    return 0;
}
