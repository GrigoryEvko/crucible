// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// with_read_view lends only a Permission, a SharedPermissionGuard or a
// ReadLoan.  A class that copies their member types, tag_type and
// brand_type, holds no right to the region, so it is not a source.  The
// door must refuse it by its family, not accept it by its shape.
//
// Expected diagnostic: no matching function for with_read_view, and the
// failed family check is_read_view_source.  Without that check the call
// compiles, because the class has every member type the door reads.

#include <foundation/permissions/ReadView.h>

namespace neg_read_view_look_alike_source {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

struct LookAlike {
    using tag_type = Region;
    using brand_type = ::foundation::brand::DefaultBrand;
};

}  // namespace neg_read_view_look_alike_source

int main() {
    using neg_read_view_look_alike_source::LookAlike;
    auto back = ::foundation::permissions::with_read_view(LookAlike{}, [](auto const&) noexcept {});
    (void)back;
    return 0;
}
