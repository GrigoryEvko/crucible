// A body of a frame of a keyed CyclicBuffer calls a helper that moves the ring
// into a local.  The end of the local ends the element that the body holds, and
// the View of its storage reads freed memory.  A keyed ring whose move writes
// its elements does not move.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>
#include <utility>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};

template <class Ring>
static std::size_t move_out(Ring& ring) noexcept {
    Ring taken{std::move(ring)};
    return taken.size();
}

int main() {
    auto minted = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<Step, 2>);
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    return ring.with_claim(key, [&ring](Step& step) noexcept {
        auto const items = step.items.view();
        static_cast<void>(move_out(ring));
        return items.size() == 0 ? 0 : 1;
    });
}
