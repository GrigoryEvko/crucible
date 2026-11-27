// Negative-compile test. This file must fail to compile.
//
// Violation: a bit cast makes the key of a foreign address from an integer.
// The owner re-mints an address from a key, so a key from eight bytes of no
// RunAddress would give an access to bytes that no foreign ABI gave.  The key
// carries no_start_over_bytes, so the bit cast of the Scalar family refuses
// it.

#include <foundation/core/DataAddress.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

namespace {
struct Owner {};
}  // namespace

int main() {
    std::uintptr_t const word = 0x1000;
    auto const key = ::foundation::core::bit_cast<::foundation::core::ForeignAddressKey<Owner>>(word);
    return key == key ? 0 : 1;
}
