// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The brand of a ReadView names the region the view was lent from.  Only
// an empty class type can be a brand.  A view branded with int would
// carry a value where the brand must carry only an identity.
//
// Expected diagnostic: the static_assert in ReadView that asks for
// IsBrand<Brand>, whose message says that a brand must be an empty class
// type.

#include <foundation/permissions/ReadView.h>

namespace neg_read_view_brand_not_empty {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_read_view_brand_not_empty

int main() {
    using neg_read_view_brand_not_empty::Region;
    return static_cast<int>(sizeof(::foundation::permissions::ReadView<Region, int>));
}
