// An update of an element that moves and does not copy.  The door gives its
// body a copy, so the ring holds a whole element while the body runs.  An
// element that does not copy has no update door.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};

int main() {
    ::fixy::CyclicBuffer<Step, 2> ring{};
    ring.push(Step{});
    ring.update(ring.next_slot(), [](Step& step) noexcept { static_cast<void>(step.items.size()); });
    return 0;
}
