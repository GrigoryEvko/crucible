// Negative-compile test. This file must fail to compile.
//
// Violation: a designated initializer gives a descriptor the rank 2 from a
// byte, and no lane beside it.  No door makes a TensorRank from a byte
// outside TensorShape, so the rank always agrees with the lanes.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const meta{.ndim = 2};
    return meta.ndim;
}
