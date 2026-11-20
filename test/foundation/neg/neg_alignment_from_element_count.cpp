// mint_alignment takes a count of bytes.  A count of elements has a
// different unit, so the alignment of an element count does not compile.

#include <foundation/alloc/Layout.h>

int main() {
    auto alignment = ::foundation::alloc::mint_alignment(::foundation::core::ElementCount{16});
    return alignment.is_ok() ? 0 : 1;
}
