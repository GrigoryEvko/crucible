// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A pointer to void names no ReadView, but it can hold the address of
// the view, and a later cast gives the view back after the door has
// returned.  ReadViewResultStaysInside refuses a result that holds an
// untyped address.
//
// Expected diagnostic: the static assertion in the door that names the
// untyped address.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace read_view_untyped_pointer_fixture {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace read_view_untyped_pointer_fixture

int main() {
    using read_view_untyped_pointer_fixture::Region;
    auto lent =
        ::foundation::permissions::with_read_view(::foundation::permissions::mint_permission_root<Region>(),
                                                  [](::foundation::permissions::ReadView<Region> const& view) noexcept {
                                                      return static_cast<void const*>(&view);
                                                  });
    (void)lent;
    return 0;
}
