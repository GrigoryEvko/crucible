// A push_new of a keyed CyclicBuffer takes no key.  It ends the element of the
// slot of the next claim, which a body of a frame of the ring could hold.  A
// keyed ring starts a new element only with its key.

#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<std::uint64_t, 4>);
    auto& ring = minted.at(::fixy::index<0>());
    ring.push_new(std::uint64_t{3});
    return 0;
}
