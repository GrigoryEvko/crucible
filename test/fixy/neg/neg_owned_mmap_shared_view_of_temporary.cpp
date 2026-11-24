// A shared view borrows the mapping.  A temporary mapping unmaps at the end
// of the full expression, so the view of a temporary is refused.

#include <fixy/os/OwnedMmap.h>

#include <cstdint>
#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    return static_cast<int>(std::move(mapping).view_at<std::uint64_t const, 4>(0).is_none());
}
