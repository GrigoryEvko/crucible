// A read of a slot of a keyed CyclicBuffer takes no key, and its body claims
// each slot with the key of the caller.  The claims end the element that the
// body reads.  A keyed ring lends a slot that a body reads only with its key.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>
#include <utility>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<Step, 2>);
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    return std::as_const(ring).with_slot(ring.next_slot(), [&ring, &key](Step const& step) noexcept {
        auto const items = step.items.view();
        ring.with_fresh_claim(key, [](Step&) noexcept {});
        ring.with_fresh_claim(key, [](Step&) noexcept {});
        return items.size() == 0 ? 0 : 1;
    });
}
