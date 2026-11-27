// Negative-compile test. This file must fail to compile.
//
// Violation: a function that is no member of the owner re-mints the address
// of a key.  The re-mint is private, and the owner is its one friend, so only
// the members of the owner reach the bytes at the address of a key.

#include <foundation/core/DataAddress.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    unsigned char byte = 0;
    auto const key = ::foundation::core::mint_foreign_address_key<Owner>(
        ::foundation::core::mint_foreign_run_address(&byte).expect("a byte of the stack is not null"));
    return ::foundation::core::ForeignAddressKey<Owner>::remint_(key, 0).is_some() ? 0 : 1;
}
