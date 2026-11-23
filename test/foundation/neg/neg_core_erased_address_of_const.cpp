// Negative-compile test. This file must fail to compile.
//
// An erased address gives its object back as a reference that can write.  A
// const object must stay const, so mint_erased_address refuses a const
// object and names its gate.
//
// Expected diagnostic: no mint_erased_address for a const int, with a note
// on the gate ErasableObject.

#include <foundation/core/CallRef.h>

int main() {
    int const count = 3;
    ::foundation::core::ErasedAddress const address = ::foundation::core::mint_erased_address(count);
    return address.restore<int>();
}
