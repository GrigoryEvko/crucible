// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A root of mint_permission_root keeps its brand.  An implicit conversion
// would let a call that takes the erased token drop a brand, with no name
// at the call site, and the erased token of the tag would then have two
// live owners.
//
// Expected diagnostic: the conversion from the branded token to the erased
// token is refused, because no constructor takes a token of another brand.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_brand_permission_does_not_erase_implicitly {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_brand_permission_does_not_erase_implicitly

int main() {
    namespace fp = ::foundation::permissions;
    using neg_brand_permission_does_not_erase_implicitly::Region;
    auto branded = fp::mint_permission_root<Region>();
    // The erased token of the tag, the type of its singleton root.
    using Erased = decltype(fp::mint_singleton_root<Region>());
    Erased erased = std::move(branded);
    ::foundation::linear::drop(std::move(erased));
    return 0;
}
