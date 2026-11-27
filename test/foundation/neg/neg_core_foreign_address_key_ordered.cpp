// Negative-compile test. This file must fail to compile.
//
// Violation: a caller orders two keys of foreign addresses.  An address
// changes from run to run, so an order of addresses gives a different answer
// in a different run.  A key compares only for equality.

#include <foundation/core/DataAddress.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    unsigned char first = 0;
    unsigned char second = 0;
    auto const low = ::foundation::core::mint_foreign_address_key<Owner>(
        ::foundation::core::mint_foreign_run_address(&first).expect("a byte of the stack is not null"));
    auto const high = ::foundation::core::mint_foreign_address_key<Owner>(
        ::foundation::core::mint_foreign_run_address(&second).expect("a byte of the stack is not null"));
    return low < high ? 0 : 1;
}
