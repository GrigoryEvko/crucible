// The hash of the Protocol family takes a leaf that is no class, such as an
// integer, and refuses a pointer.  An address changes from run to run, so a
// hash of it breaks the bit-exact replay.  The template argument gives the
// pointer its type.

#include <foundation/core/Protocol.h>

template <class Address>
int hash_bit_of(Address address) {
    return static_cast<int>(::foundation::core::hash(address).raw() & 1u);
}

int main() {
    int const target = 1;
    return hash_bit_of(&target);
}
