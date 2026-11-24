// A body of the claim door keeps a Ref of its slot in an Option that it
// captures by reference.  A later claim reuses the slot, so the kept Ref
// would name a later element, and a second Ref of one slot would be a second
// writer.  The store check of the door refuses the body (CyclicBody).

#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>

#include <cstdint>

int main() {
    ::fixy::CyclicBuffer<std::uint64_t, 4> ring{};
    ::fixy::Option<::fixy::Ref<std::uint64_t>> kept{::fixy::none};
    ring.with_claim([&kept](std::uint64_t& slot) noexcept {
        slot = 7;
        kept = ::fixy::some(::fixy::erase_brand(::fixy::to(slot)));
    });
    return 0;
}
