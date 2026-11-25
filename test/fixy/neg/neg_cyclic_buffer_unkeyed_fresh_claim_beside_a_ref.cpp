// A body of the fresh claim door of a ring with no key claims each slot of the
// ring through a Ref of the ring.  The claims end the element that the body
// holds, and the View of its storage reads freed memory.  A ring with no key
// lends no slot that a body writes.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};
using Ring = ::fixy::CyclicBuffer<Step, 2>;

int main() {
    Ring ring{};
    auto const other = ::fixy::to(ring);
    return ring.with_fresh_claim([other](Step& step) noexcept {
        auto const items = step.items.view();
        other->push(Step{});
        other->push(Step{});
        return items.size() == 0 ? 0 : 1;
    });
}
