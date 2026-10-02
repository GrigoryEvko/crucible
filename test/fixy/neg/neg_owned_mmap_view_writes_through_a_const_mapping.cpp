// A const mapping gives only a View of const elements.  A View that writes
// needs a mutable borrow of the mapping.

#include <fixy/OwnedMmap.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadWrite, ::fixy::mmap::share::Anonymous, RegionBrand>;
}  // namespace

int main() {
    Mapping const mapping{};
    return static_cast<int>(mapping.view<int>().size());
}
