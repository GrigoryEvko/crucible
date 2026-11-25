// A ticket is the claim of a write below 2^62, so each ticket has its own
// commit words.  A number is no ticket: check() and mask() of RingTicket make
// one from the write index of the ring.

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
    std::uint64_t const write_index = 8;
    for (::fixy::mmap::SharedView<Event const, 4> const ring : mapping.view_at<Event const, 4>(0)) {
        for (Event const event : ring.read_ticket<&Event::commit>(write_index - 1)) {
            return static_cast<int>(event.value);
        }
    }
    return 0;
}
