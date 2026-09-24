// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidNDim receives UINT8_MAX in constant
// evaluation.  This is the wide miss of the ndim cap.
//
// ValidNDim is ::fixy::Refined<::fixy::bounded_above<kMaxTensorNDim>,
// uint8_t>, and kMaxTensorNDim is 8.  Without the gate, a payload that
// supplies ndim 255 passes read_meta and overruns sizes[8] and strides[8]
// in the first loop over the dimensions.
//
// The companion fixture neg_tensor_meta_ndim_above_max.cpp is the boundary
// edge (kMaxTensorNDim + 1).  This one catches a ValidNDim that loses its
// bound and becomes a plain uint8_t.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <climits>
#include <cstdint>

int main() {
    constexpr crucible::ValidNDim bad =
        ::fixy::mint_refined<::fixy::bounded_above<crucible::kMaxTensorNDim>>(static_cast<std::uint8_t>(UINT8_MAX));
    (void)bad;
    return 0;
}
