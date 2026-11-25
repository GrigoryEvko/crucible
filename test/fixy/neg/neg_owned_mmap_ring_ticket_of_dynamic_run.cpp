// A run of the dynamic extent has no count in its type, so no mask of a ticket
// proves a slot of it, and it has no write_ticket.  window<Count>() gives the
// run of a fixed extent.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
struct Event {
    std::uint64_t value = 0;
    std::uint64_t commit = 0;
};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadWrite, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    for (::fixy::mmap::SharedView<Event> const ring : mapping.view<Event>()) {
        static_cast<void>(ring.write_ticket<&Event::commit>(::fixy::mmap::RingTicket::mask(0), Event{}));
    }
    return 0;
}
