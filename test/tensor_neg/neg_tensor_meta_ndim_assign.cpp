// Negative-compile test. This file must fail to compile.
//
// Violation: a descriptor takes the rank of another descriptor and keeps its
// own lanes.  The rank then disagrees with the lanes, so the copy assignment
// of TensorRank is private to TensorMeta and TensorShape, and
// TensorMeta::set_shape writes the rank and the lanes together.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta target{};
    target.ndim = source.ndim;
    return target.ndim;
}
