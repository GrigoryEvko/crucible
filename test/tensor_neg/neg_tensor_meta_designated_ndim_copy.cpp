// Negative-compile test. This file must fail to compile.
//
// Violation: a designated initializer gives a descriptor the rank of another
// descriptor, with no lanes beside it.  The rank must agree with the lanes,
// so only TensorMeta::set_shape writes the rank, and the copy constructor of
// TensorRank is private to TensorMeta and TensorShape.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta const copied{.ndim = source.ndim};
    return copied.ndim;
}
