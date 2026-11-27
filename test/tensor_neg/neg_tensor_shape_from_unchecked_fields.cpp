// Negative-compile test. This file must fail to compile.
//
// Violation: a caller builds a TensorShape from the fields of a writer
// outside the process with no check of their lanes.  The constructor takes
// fields that decode_tensor_shape checked, so it is private to that door,
// which refuses a rank past kMaxTensorNDim, a lane past kMaxTensorDimExtent
// and a lane past the rank that is not zero.

#include <crucible/TensorMeta.h>

int main() {
    crucible::UncheckedTensorMeta fields{};
    fields.ndim = 1;
    const crucible::TensorShape shape{fields};
    return shape.ndim();
}
