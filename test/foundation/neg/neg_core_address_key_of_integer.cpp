// mint_address_key takes a pointer, so no integer becomes a key.  A key
// from an integer would name an address that no pointer held.

#include <foundation/core/Ref.h>

#include <cstdint>

int main() {
    std::uintptr_t const word = 4096;
    static_cast<void>(::foundation::core::mint_address_key(word));
    return 0;
}
