// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidNDim receives kMaxTensorNDim + 1 in
// constant evaluation.  This is the boundary edge of the ndim cap.
//
// ValidNDim is ::fixy::Refined<::fixy::bounded_above<kMaxTensorNDim>,
// uint8_t>, and kMaxTensorNDim is 8, the length of the inline sizes and
// strides arrays.  A value in [9, 255] is corrupt or adversarial, and every
// consumer that walks sizes or strides assumes meta.ndim <= 8.
//
// The companion fixture neg_tensor_meta_ndim_uint8_max.cpp is the wide miss
// (UINT8_MAX).  This one catches a bound that widens by any K >= 1.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidNDim bad = ::fixy::mint_refined<::fixy::bounded_above<crucible::kMaxTensorNDim>>(
        static_cast<std::uint8_t>(crucible::kMaxTensorNDim + 1u));
    (void)bad;
    return 0;
}
