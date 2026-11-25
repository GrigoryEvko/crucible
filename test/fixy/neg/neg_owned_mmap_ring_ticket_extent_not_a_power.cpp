// The slot of a ring ticket is the ticket masked to the extent of the run, so
// a run of three records has no read_ticket: the mask would give two tickets of
// one lap the same slot and leave one slot with no ticket.

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
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    for (::fixy::mmap::SharedView<Event const, 3> const ring : mapping.view_at<Event const, 3>(0)) {
        for (Event const event : ring.read_ticket<&Event::commit>(::fixy::mmap::RingTicket::mask(0))) {
            return static_cast<int>(event.value);
        }
    }
    return 0;
}
