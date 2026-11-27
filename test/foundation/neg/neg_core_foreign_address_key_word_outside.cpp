// Negative-compile test. This file must fail to compile.
//
// Violation: a function that is no member of the owner makes the key of a
// word.  The door of a word is private, and the owner is its one friend, so
// each key of a word comes from a door of the owner, such as the mint of a
// synthetic address of a test.

#include <foundation/core/DataAddress.h>

namespace {
struct Owner {};
}  // namespace

int main() {
    auto const key = ::foundation::core::ForeignAddressKey<Owner>::of_word_(0x1000);
    return key == key ? 0 : 1;
}
