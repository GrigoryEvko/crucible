// mint_layout takes a ByteCount and an Alignment, and no number converts to
// either one.  So a call with two numbers, which can swap with no error,
// does not compile.

#include <foundation/alloc/Layout.h>

int main() {
    auto layout = ::foundation::alloc::mint_layout(16, 8);
    return layout.is_ok() ? 0 : 1;
}
