// Negative-compile test. This file must fail to compile.
//
// Violation: a call of mint_tensor_shape gives the strides in the place of
// the sizes.  The sizes and the strides have their own types, and the
// constraint TensorShapeLanes takes only the sizes first and the strides
// second.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    const ::fixy::FixedArray<crucible::TensorDim, 1> lanes{crucible::tensor_dim(4)};
    const auto shape =
        crucible::mint_tensor_shape(crucible::TensorStrides{lanes.view()}, crucible::TensorSizes{lanes.view()});
    return shape.is_none() ? 1 : 0;
}
