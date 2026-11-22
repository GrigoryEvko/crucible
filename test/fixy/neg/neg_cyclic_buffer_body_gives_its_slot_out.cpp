// A body of the claim door gives a Ref of its slot out as its result.  A later
// claim reuses the slot, so the Ref would name a later element, and a second
// Ref of one slot would be a second writer.  The result of a door of a ring
// holds no reference and no address.

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    ::fixy::CyclicBuffer<std::uint64_t, 4> ring{};
    auto const held = ring.with_claim([](std::uint64_t& slot) noexcept { return ::fixy::to(slot); });
    static_cast<void>(held);
    return 0;
}
