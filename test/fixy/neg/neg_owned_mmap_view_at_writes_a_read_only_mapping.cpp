// A read-only mapping holds pages that the kernel mapped without write
// access.  A View at an offset with mutable elements would write them, so
// the gate of view_at asks for a View of const elements.

#include <fixy/os/OwnedMmap.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Private, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    return static_cast<int>(mapping.view_at<int, 4>(16).is_none());
}
