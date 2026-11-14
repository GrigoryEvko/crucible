// An assignment over a live region would write over its Permission, and the
// move assignment of a Permission does nothing.  So the token of the target
// would leave through no door, and the assignment is refused.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Row.h>

namespace owned_region_assignment {

struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};

// The two regions share one brand, so that only the assignment is wrong.
struct CacheRoot {};

using Region = ::fixy::OwnedRegion<int, Cache, CacheRoot>;

void assign_over(Region& target, Region&& source) noexcept;

void assign_over(Region& target, Region&& source) noexcept { target = static_cast<Region&&>(source); }

}  // namespace owned_region_assignment

int main() { return 0; }
