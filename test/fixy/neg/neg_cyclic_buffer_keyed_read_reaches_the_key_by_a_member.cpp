// A read of a slot of a keyed CyclicBuffer borrows the key, and its body
// captures an object that holds a reference to the key.  A method of that
// object claims each slot, and the claims end the element that the body reads.
// The door refuses a body that reaches the key, also through a member.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>
#include <utility>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};

template <class Ring, class Key>
struct Claimer {
    Ring& ring;
    Key& key;

    void claim_each_slot() const noexcept {
        ring.with_fresh_claim(key, [](Step&) noexcept {});
        ring.with_fresh_claim(key, [](Step&) noexcept {});
    }
};

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<Step, 2>);
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    Claimer const claimer{ring, key};
    return std::as_const(ring).with_slot(key, ring.next_slot(), [&claimer](Step const& step) noexcept {
        auto const items = step.items.view();
        claimer.claim_each_slot();
        return items.size() == 0 ? 0 : 1;
    });
}
