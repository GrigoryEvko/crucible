// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-TensorMeta-1 (#1034): TensorDim construction is gated by
// bounded_above<kMaxTensorDimExtent>, where the bound is INT64_MAX / 16
// for the largest ScalarType element width.  The boundary value + 1
// must fail in constexpr context.
//
// Expected diagnostic: Refined bounded_above contract failure.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
  constexpr auto bad =
      crucible::tensor_dim(
          crucible::kMaxTensorDimExtent + std::int64_t{1});
  (void)bad;
  return 0;
}
