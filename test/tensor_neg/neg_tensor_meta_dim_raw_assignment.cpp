// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TensorMeta sizes/strides write sites
// require TensorDim = Refined<bounded_above<kMaxTensorDimExtent>,
// int64_t>.  A raw integer literal must not assign directly into the
// dimension lane.
//
// Expected diagnostic: no viable operator= from int to TensorDim slot.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta meta{};
    meta.sizes[0] = 8;
    return 0;
}
