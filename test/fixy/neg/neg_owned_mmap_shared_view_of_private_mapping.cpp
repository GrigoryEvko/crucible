// A private mapping holds a copy of the pages for this process alone, so no
// other writer stores its bytes, and view() of it gives a plain View.  No
// SharedView of it converts from that.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Private, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    ::foundation::core::Option<::fixy::mmap::SharedView<std::uint64_t const>> const shared =
        mapping.view<std::uint64_t const>();
    return static_cast<int>(shared.is_none());
}
