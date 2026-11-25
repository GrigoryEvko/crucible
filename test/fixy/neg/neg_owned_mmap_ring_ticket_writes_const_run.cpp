// A run of const records is the side of a reader: the mapping of a ring that a
// BPF program writes is read only, so the run has no write_ticket.

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
    for (::fixy::mmap::SharedView<Event const, 4> const ring : mapping.view_at<Event const, 4>(0)) {
        static_cast<void>(ring.write_ticket<&Event::commit>(::fixy::mmap::RingTicket::mask(0), Event{}));
    }
    return 0;
}
