// A body of the claim door of a keyed CyclicBuffer captures the moved key
// and the ring, and opens the slot door of the slot that the claim lends.
// The two bodies would hold two writers of one slot.  The door refuses a body
// that reaches the key of the ring.

#include <fixy/CyclicBuffer.h>

#include <cstdint>
#include <utility>

int main() {
    auto minted = ::fixy::mint_cyclic_buffer<std::uint64_t, 4>();
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    key = ring.with_claim(std::move(key), [&ring, &key](std::uint64_t& claimed) noexcept {
        ring.with_slot(key, ring.slot_back(::fixy::index<0>()), [](std::uint64_t& again) noexcept { again = 2; });
        claimed = 1;
    });
    return 0;
}
