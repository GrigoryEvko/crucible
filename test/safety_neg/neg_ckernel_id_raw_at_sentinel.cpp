// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ValidCKernelIdRaw is Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>.
// It is the gate at every widening of a uint8_t to a CKernelId.  The one
// door into the type, mint_refined<kValidCKernelIdBound>, must refuse the
// value of the NUM_KERNELS sentinel, because the sentinel is a count and
// never a real kernel.
//
// This fixture is the edge of the bound.  It fails when the bound widens
// from NUM_KERNELS - 1 to NUM_KERNELS.  The companion fixture
// neg_ckernel_id_raw_uint8_max is the wide miss, and it fails when the
// bound goes away.
//
// Expected diagnostic: the bounded_above precondition of mint_refined
// fails in a constant expression.

#include <crucible/CKernel.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidCKernelIdRaw bad =
        ::fixy::mint_refined<crucible::kValidCKernelIdBound>(static_cast<uint8_t>(crucible::CKernelId::NUM_KERNELS));
    (void)bad;
    return 0;
}
