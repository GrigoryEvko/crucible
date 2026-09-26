// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidRegsPerThread is a fixy::Refined over valid_regs_per_thread, a
// ceiling of 255, and the checked mint is its only door.  This fixture
// gives the mint 65535, the largest uint16_t.  It catches a caller that
// passes a sentinel maximum or a value read from a damaged snapshot.
//
// The companion fixture neg_costmodel_regs_per_thread_overflow gives the
// mint 256, one above the ceiling.

#include <crucible/CostModel.h>

#include <fixy/Refined.h>

#include <cstdint>
#include <limits>

int main() {
    constexpr crucible::ValidRegsPerThread bad =
        ::fixy::mint_refined<crucible::valid_regs_per_thread>(std::numeric_limits<uint16_t>::max());
    (void)bad;
    return 0;
}
