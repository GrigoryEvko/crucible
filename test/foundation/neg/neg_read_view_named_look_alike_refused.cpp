// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A named class that copies the member types of a source is not a
// source, so the door refuses it by its family.  The deleted twin, which
// tells the caller to pass the source with std::move, asks for a real
// source too.  Without that condition the twin takes the named look-alike,
// and its message sends the caller to std::move a value that is still
// not a source.
//
// Expected diagnostic: no matching function for with_read_view, and the
// failed family check is_read_view_source.

#include <foundation/permissions/ReadView.h>

namespace neg_read_view_named_look_alike {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

struct LookAlike {
    using tag_type = Region;
    using brand_type = ::foundation::brand::DefaultBrand;
};

}  // namespace neg_read_view_named_look_alike

int main() {
    neg_read_view_named_look_alike::LookAlike named{};
    ::foundation::permissions::with_read_view(named, [](auto const&) noexcept {});
    return 0;
}
