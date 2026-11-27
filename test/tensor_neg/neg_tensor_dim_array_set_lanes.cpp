// Negative-compile test. This file must fail to compile.
//
// Violation: a caller writes each lane of a block of a descriptor from a
// block with no check.  The write of a whole block takes a block that
// decode_tensor_shape checked, so it is private, and only the constructor of
// TensorShape from checked fields calls it.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    crucible::TensorMeta meta{};
    ::fixy::FixedArray<int64_t, crucible::kMaxTensorNDim> lanes{};
    meta.sizes.set_lanes_(lanes);
    return meta.ndim;
}
