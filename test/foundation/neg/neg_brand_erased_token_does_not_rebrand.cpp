// Erasure runs one way.  The singleton root of a tag is on the erased
// identity, and nothing converts an erased token to a brand.  So no code can
// use the erased identity to make a token of an instance that the caller does
// not hold.

#include <foundation/permissions/Permission.h>

#include <utility>

// The tag of a singleton root has a stable identity, so it has external
// linkage.
namespace neg_brand_erased_token_does_not_rebrand_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HeldBrand {};
}  // namespace neg_brand_erased_token_does_not_rebrand_types

using namespace neg_brand_erased_token_does_not_rebrand_types;

int main() {
    ::foundation::permissions::Permission<Region> erased = ::foundation::permissions::mint_singleton_root<Region>();
    ::foundation::permissions::Permission<Region, HeldBrand> rebranded{std::move(erased)};
    (void)rebranded;
    return 0;
}
