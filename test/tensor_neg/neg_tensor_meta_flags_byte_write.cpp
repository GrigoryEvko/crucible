// Negative-compile test. This file must fail to compile.
//
// Violation: a caller writes a byte with the reserved bit 6 into the flags of
// a descriptor.  TensorMeta::set_flags is the one writer of the flags, and it
// writes zero in each reserved bit, so no assignment takes a byte.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta meta{};
    meta.flags = uint8_t{0x40};
    return meta.flags;
}
