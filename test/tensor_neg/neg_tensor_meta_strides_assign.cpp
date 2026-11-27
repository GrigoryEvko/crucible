// Negative-compile test. This file must fail to compile.
//
// Violation: a descriptor takes the strides of another descriptor and keeps
// its own rank.  The lanes past the rank of the target can then hold strides,
// so the copy assignment of TensorDimArray is private to TensorMeta and
// TensorShape, and TensorMeta::set_shape writes the lanes and the rank
// together.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta target{};
    target.strides = source.strides;
    return target.ndim;
}
