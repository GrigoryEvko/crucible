// A body of the slot door of a ring with no key pushes into each slot of the
// ring.  The pushes end the storage of the element that the body holds, and the
// View of it reads freed memory.  A ring with no key lends no slot that a body
// writes.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};
using Ring = ::fixy::CyclicBuffer<Step, 2>;

int main() {
    Ring ring{};
    ring.push(Step{});
    return ring.with_slot(ring.slot_back(::fixy::index<0>()), [&ring](Step& step) noexcept {
        auto const items = step.items.view();
        ring.push(Step{});
        ring.push(Step{});
        return items.size() == 0 ? 0 : 1;
    });
}
