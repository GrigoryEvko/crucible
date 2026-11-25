// A read of a slot of a keyed CyclicBuffer borrows the key, and its body
// captures the key by reference and claims each slot.  The claims end the
// element that the body reads.  The door refuses a body that reaches the key.

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
    return std::as_const(ring).with_slot(key, ring.next_slot(), [&ring, &key](Step const& step) noexcept {
        auto const items = step.items.view();
        ring.with_fresh_claim(key, [](Step&) noexcept {});
        ring.with_fresh_claim(key, [](Step&) noexcept {});
        return items.size() == 0 ? 0 : 1;
    });
}
