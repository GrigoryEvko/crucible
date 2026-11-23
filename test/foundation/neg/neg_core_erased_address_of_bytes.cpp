// Negative-compile test. This file must fail to compile.
//
// An erased address holds the address of an object whose type a holder
// erases.  The address of bytes erases no type, and a holder could read the
// bytes back as a different object.  So mint_erased_address refuses a byte
// and names its gate.
//
// Expected diagnostic: no mint_erased_address for an unsigned char, with a
// note on the gate ErasableObject.

#include <foundation/core/CallRef.h>

int main() {
    unsigned char storage = 0;
    ::foundation::core::ErasedAddress const address = ::foundation::core::mint_erased_address(storage);
    return static_cast<int>(address.restore<int>());
}
