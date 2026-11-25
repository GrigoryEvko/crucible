// An update of a keyed CyclicBuffer takes no key.  A frame of another name of
// the ring could hold the slot, and the copy would then end the element that
// the body of that frame holds.  A keyed ring has no update door.

#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<std::uint64_t, 4>);
    auto& ring = minted.at(::fixy::index<0>());
    ring.update(ring.next_slot(), [](std::uint64_t& element) noexcept { element = 2; });
    return 0;
}
