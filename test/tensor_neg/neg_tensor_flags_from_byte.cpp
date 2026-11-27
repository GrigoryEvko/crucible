// Negative-compile test. This file must fail to compile.
//
// Violation: a caller builds TensorFlags from a byte and gives them to a
// descriptor.  The constructor from a byte writes zero in each reserved bit,
// and it is private to TensorMeta, so set_flags is the one road from a byte.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const meta{.flags = crucible::TensorFlags{uint8_t{0x40}}};
    return meta.flags;
}
