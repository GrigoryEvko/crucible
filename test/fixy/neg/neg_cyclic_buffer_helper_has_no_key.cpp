// A helper takes a keyed CyclicBuffer, and no key, and writes a slot.  A body
// of the claim door calls the helper, as the route of a second writer through
// a callee does.  A keyed ring lends a slot that a body writes only with its
// key, and the helper has none.

#include <fixy/CyclicBuffer.h>

#include <cstdint>

namespace {

template <class Ring>
void write_without_key(Ring& ring) noexcept {
    ring.with_slot(ring.slot_back(::fixy::index<0>()), [](std::uint64_t& slot) noexcept { slot = 2; });
}

}  // namespace

int main() {
    auto minted = ::fixy::mint_cyclic_buffer<std::uint64_t, 4>();
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    ring.with_claim(key, [&ring](std::uint64_t& claimed) noexcept {
        write_without_key(ring);
        claimed = 1;
    });
    return 0;
}
