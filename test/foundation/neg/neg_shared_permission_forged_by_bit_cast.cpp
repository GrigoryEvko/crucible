// A share built from a byte.  A share whose constructors and assignments
// are all trivial is trivially copyable, and std::bit_cast builds any such
// type from bytes with no constructor call, so the private constructor of
// the share did not stop it.  The assignments of a share are user-provided,
// so the share is not trivially copyable, and std::bit_cast has no
// candidate.  The copy constructor stays trivial, so a copy of a share
// costs nothing.

#include <foundation/permissions/Permission.h>

#include <bit>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct RegionBrand {};
}  // namespace

int main() {
    auto forged = std::bit_cast<fp::SharedPermission<Region, RegionBrand>>(char{0});
    (void)forged;
    return 0;
}
