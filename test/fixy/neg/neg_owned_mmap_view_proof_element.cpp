// A View of a mapping starts the lifetime of its elements over the bytes
// of the mapping.  An element that holds a proof would get a proof that no
// door built, so the gate refuses an element that is not an
// implicit-lifetime type at each depth.

#include <fixy/OwnedMmap.h>
#include <foundation/Lifetime.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
using Mapping = ::fixy::OwnedMmap<Region, ::fixy::mmap::prot::ReadWrite, ::fixy::mmap::share::Anonymous, RegionBrand>;
}  // namespace

int main() {
    Mapping mapping{};
    return static_cast<int>(mapping.view<::foundation::lifetime::detail::HoldsProof>().size());
}
