// Negative-compile test. This file must fail to compile.
//
// Violation: a brace initializer with no designators gives a descriptor the
// sizes of another descriptor as its first member, with the rank left at
// zero.  The copy constructor of TensorDimArray is private to TensorMeta and
// TensorShape, so this road is shut as the designated one is.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const source{};
    crucible::TensorMeta const copied{source.sizes};
    return copied.ndim;
}
