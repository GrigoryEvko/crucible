// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A permission token drops its brand only through permission_erase_brand.
// An implicit conversion lets a call that takes the erased token drop a
// brand, with no name at the call site.  A search for the door then does
// not find that site.
//
// Expected diagnostic: the conversion from the branded token to the erased
// token is refused, because the erasure constructor asks for the key that
// only permission_erase_brand makes.

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
    // The type that the door returns, the erased token of the tag.
    using Erased = decltype(fp::permission_erase_brand(fp::mint_permission_root<Region>()));
    Erased erased = std::move(branded);
    fp::permission_drop(std::move(erased));
    return 0;
}
