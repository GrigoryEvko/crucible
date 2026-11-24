// Another writer stores the bytes of a shared mapping, so a plain View of it
// would read with plain loads: a data race.  view() of a mapping of the share
// Shared gives a SharedView, and no plain View of it converts from that.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    ::foundation::core::Option<::foundation::core::View<std::uint64_t const>> const plain =
        mapping.view<std::uint64_t const>();
    return static_cast<int>(plain.is_none());
}
