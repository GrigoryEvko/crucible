// Negative-compile test. This file must fail to compile.
//
// ValidCKernelIdRaw is Refined<bounded_above<ckernel_id_count - 1>, uint8_t>.
// It is the gate at every widening of a uint8_t to a CKernelId.  The one
// door into the type, mint_refined<kValidCKernelIdBound>, must refuse the
// byte of the count of the kernel ids, because that byte names no kernel.
//
// This fixture is the edge of the bound.  It fails when the bound widens
// from ckernel_id_count - 1 to ckernel_id_count.  The companion fixture
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
        ::fixy::mint_refined<crucible::kValidCKernelIdBound>(static_cast<uint8_t>(crucible::ckernel_id_count));
    (void)bad;
    return 0;
}
