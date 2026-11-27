// Negative-compile test. This file must fail to compile.
//
// Violation: a descriptor takes the sizes of another descriptor and keeps its
// own rank.  The lanes past the rank of the target can then hold sizes, so
// the copy assignment of TensorDimArray is private to TensorMeta and
// TensorShape, and TensorMeta::set_shape writes the lanes and the rank
// together.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta target{};
    target.sizes = source.sizes;
    return target.ndim;
}
