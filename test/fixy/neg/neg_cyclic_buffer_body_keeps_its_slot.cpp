// A body of the update door keeps a Ref of its copy in an Option that it
// captures by reference.  The copy ends with the door, so the kept Ref would
// name no element.  The store check of the door refuses the body (CyclicBody).

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    ::fixy::CyclicBuffer<std::uint64_t, 4> ring{};
    ::fixy::Option<::fixy::Ref<std::uint64_t>> kept{::fixy::none};
    ring.update(ring.next_slot(), [&kept](std::uint64_t& copy) noexcept {
        copy = 7;
        kept = ::fixy::some(::fixy::erase_brand(::fixy::to(copy)));
    });
    return 0;
}
