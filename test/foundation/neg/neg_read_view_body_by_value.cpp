// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body that takes the view by value asks the door for a copy or a
// move of the view, and ReadView has neither.  The door takes a body
// that takes the view by const reference, so the body is refused.
//
// Expected diagnostic: no matching function for with_read_view, whose
// candidate was discarded because ReadViewBody is not satisfied.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto back = ::foundation::permissions::with_read_view(::foundation::permissions::mint_permission_root<Region>(),
                                                          [](::foundation::permissions::ReadView<Region>) noexcept {});
    (void)back;
    return 0;
}
