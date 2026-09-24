// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidCKernelIdRaw is Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>.
// The byte 0xFF is past every kernel id, and it would name a CKernelId
// enumerator that does not exist.  The door mint_refined must refuse it.
//
// This fixture is the wide miss.  It fails when ValidCKernelIdRaw becomes
// a plain uint8_t alias, which would admit every byte from a damaged
// trace file.  The companion fixture neg_ckernel_id_raw_at_sentinel is
// the edge of the bound.
//
// Expected diagnostic: the bounded_above precondition of mint_refined
// fails in a constant expression.

#include <crucible/CKernel.h>
#include <fixy/Refined.h>

#include <climits>
#include <cstdint>

int main() {
    constexpr crucible::ValidCKernelIdRaw bad = ::fixy::mint_refined<crucible::kValidCKernelIdBound>(uint8_t{UINT8_MAX});
    (void)bad;
    return 0;
}
