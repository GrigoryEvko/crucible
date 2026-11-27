// Negative-compile test. This file must fail to compile.
//
// Violation: a designated initializer gives a descriptor the sizes of another
// descriptor, with the rank left at zero.  Each lane past the rank must hold
// zero, so only TensorMeta::set_shape writes the lanes, and the copy
// constructor of TensorDimArray is private to TensorMeta and TensorShape.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta const copied{.sizes = source.sizes};
    return copied.ndim;
}
