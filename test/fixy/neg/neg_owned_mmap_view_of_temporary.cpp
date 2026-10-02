// A View of a mapping borrows the mapping.  A temporary mapping unmaps at
// the end of the full expression, and the View would dangle, so the
// rvalue overload of view() is deleted.

#include <fixy/OwnedMmap.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadWrite, ::fixy::mmap::share::Anonymous, RegionBrand>;
}  // namespace

int main() { return static_cast<int>(Mapping{}.view<int>().size()); }
