// A body of the claim door of a ring with no key calls a helper that claims
// each slot of the ring.  The claims end the element that the body holds, and
// the View of its storage reads freed memory.  A ring with no key lends no
// slot that a body writes.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

struct Step {
    ::fixy::HeapArray<std::uint64_t> items{};
};
using Ring = ::fixy::CyclicBuffer<Step, 2>;

static void claim_each_slot(Ring& ring) noexcept {
    ring.push(Step{});
    ring.push(Step{});
}

int main() {
    Ring ring{};
    return ring.with_claim([&ring](Step& step) noexcept {
        auto const items = step.items.view();
        claim_each_slot(ring);
        return items.size() == 0 ? 0 : 1;
    });
}
