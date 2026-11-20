// mint_alignment takes a ByteCount, and no number converts to one.  So the
// alignment of a bare number does not compile: the number has no unit.

#include <foundation/alloc/Layout.h>

int main() {
    auto alignment = ::foundation::alloc::mint_alignment(16);
    return alignment.is_ok() ? 0 : 1;
}
