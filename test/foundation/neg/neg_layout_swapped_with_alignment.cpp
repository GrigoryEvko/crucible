// mint_layout takes the bytes first and the alignment second, and the two
// have different types.  So a call that swaps them does not compile.

#include <foundation/alloc/Layout.h>

int main() {
    auto layout =
        ::foundation::alloc::mint_layout(::foundation::alloc::Alignment{8}, ::foundation::core::ByteCount{16});
    return layout.is_ok() ? 0 : 1;
}
