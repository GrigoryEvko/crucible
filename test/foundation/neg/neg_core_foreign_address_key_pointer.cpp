// Negative-compile test. This file must fail to compile.
//
// Violation: a caller reads the pointer of the key of a foreign address.  No
// pointer and no integer comes back from a key, so only the members of its
// owner reach the bytes at its address.

#include <foundation/core/DataAddress.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    unsigned char byte = 0;
    auto const key = ::foundation::core::mint_foreign_address_key<Owner>(
        ::foundation::core::mint_foreign_run_address(&byte).expect("a byte of the stack is not null"));
    return ::foundation::core::foreign_pointer(key) == nullptr ? 1 : 0;
}
