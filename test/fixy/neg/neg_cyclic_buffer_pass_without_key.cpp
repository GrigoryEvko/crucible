// A keyed CyclicBuffer passes a slot with no key.  The pass writes the claim
// cursor of the ring, so each slot that a body holds names a later element
// after it.  Each writer of a keyed ring takes its key, also a pass that
// writes no element.

#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    auto minted = ::fixy::mint_cyclic_buffer<std::uint64_t, 4>();
    auto& ring = minted.at(::fixy::index<0>());
    ring.pass_slot();
    return 0;
}
