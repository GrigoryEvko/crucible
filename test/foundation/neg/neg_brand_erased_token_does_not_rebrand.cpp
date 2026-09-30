// Erasure runs one way.  permission_erase_brand drops a brand, and
// nothing converts an erased token to a brand.  So no code can use the
// erased identity to make a token of an instance that the caller does
// not hold.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HeldBrand {};
}  // namespace

int main() {
    ::foundation::permissions::Permission<Region> erased =
        ::foundation::permissions::permission_erase_brand(::foundation::permissions::mint_permission_root<Region>());
    ::foundation::permissions::Permission<Region, HeldBrand> rebranded{std::move(erased)};
    (void)rebranded;
    return 0;
}
