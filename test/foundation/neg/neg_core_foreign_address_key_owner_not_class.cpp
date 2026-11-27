// Negative-compile test. This file must fail to compile.
//
// Violation: the mint of the key of a foreign address names an integer as
// the owner.  Only the members of the owner re-mint an address from a key, so
// the owner is a class.

#include <foundation/core/DataAddress.h>

int main() {
    unsigned char byte = 0;
    auto const key = ::foundation::core::mint_foreign_address_key<int>(
        ::foundation::core::mint_foreign_run_address(&byte).expect("a byte of the stack is not null"));
    return key == key ? 0 : 1;
}
