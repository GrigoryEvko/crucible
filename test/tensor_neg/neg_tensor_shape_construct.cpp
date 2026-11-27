// Negative-compile test. This file must fail to compile.
//
// Violation: a caller builds a TensorShape from lanes with no check of their
// counts.  The constructor takes lanes that mint_tensor_shape checked, so it
// is private to the mint, which refuses two counts that differ or a count
// past kMaxTensorNDim.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    const ::fixy::FixedArray<crucible::TensorDim, 2> sizes{crucible::tensor_dim(4), crucible::tensor_dim(3)};
    const ::fixy::FixedArray<crucible::TensorDim, 1> strides{crucible::tensor_dim(1)};
    const crucible::TensorShape shape{crucible::TensorSizes{sizes.view()}, crucible::TensorStrides{strides.view()}};
    return shape.ndim();
}
