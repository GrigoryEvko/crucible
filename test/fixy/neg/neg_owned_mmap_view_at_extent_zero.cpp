// A View of a fixed extent holds at least one element, so the fixed form
// of view_at refuses the extent zero.  A View of no element comes from
// the form with a count.

#include <fixy/os/OwnedMmap.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadWrite, ::fixy::mmap::share::Anonymous, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    return static_cast<int>(mapping.view_at<int, 0>(0).is_none());
}
