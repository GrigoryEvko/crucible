// Negative-compile test. This file must fail to compile.
//
// Violation: a call of mint_tensor_shape gives two plain views of lanes.  A
// plain view does not tell the sizes from the strides, so the constraint
// TensorShapeLanes takes only a TensorSizes and a TensorStrides.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    const ::fixy::FixedArray<crucible::TensorDim, 1> lanes{crucible::tensor_dim(4)};
    const ::fixy::View<crucible::TensorDim const> view = lanes.view();
    const auto shape = crucible::mint_tensor_shape(view, view);
    return shape.is_none() ? 1 : 0;
}
