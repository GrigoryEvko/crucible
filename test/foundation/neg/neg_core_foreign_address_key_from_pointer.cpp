// Negative-compile test. This file must fail to compile.
//
// Violation: the mint of the key of a foreign address takes a raw pointer.
// The mint takes only a RunAddress, whose mints refuse the null pointer, so
// each key holds an address that is not null.

#include <foundation/core/DataAddress.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    unsigned char byte = 0;
    auto const key = ::foundation::core::mint_foreign_address_key<Owner>(&byte);
    return key == key ? 0 : 1;
}
