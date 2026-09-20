// Erasure runs one way.  A branded token converts to the erased
// spelling, and nothing converts an erased token to a brand, so the
// erased identity cannot be used to manufacture a token of an
// instance the caller does not hold.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HeldBrand {};
}  // namespace

int main() {
    ::foundation::permissions::Permission<Region> erased = ::foundation::permissions::mint_permission_root<Region>();
    ::foundation::permissions::Permission<Region, HeldBrand> rebranded{std::move(erased)};
    (void)rebranded;
    return 0;
}
