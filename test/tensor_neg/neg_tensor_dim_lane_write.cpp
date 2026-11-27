// Negative-compile test. This file must fail to compile.
//
// Violation: a caller writes one lane of the sizes of a descriptor with a
// checked TensorDim.  The lane holds a TensorDim, but a lane past the rank
// must hold zero, so no subscript writes a lane: the subscript gives a
// ConstSlot, and TensorMeta::set_shape writes the lanes and the rank
// together.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta meta{};
    meta.sizes[3] = crucible::tensor_dim(4);
    return meta.ndim;
}
