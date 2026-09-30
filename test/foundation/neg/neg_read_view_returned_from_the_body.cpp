// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// What a body returns leaves the frame of the door.  A body that returns
// a pointer to its view hands out a pointer that dangles when the door
// returns.  ReadViewResultStaysInside refuses a result that names a
// ReadView anywhere the walk reaches.
//
// Expected diagnostic: the static assertion in the door that names the
// result.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto lent = ::foundation::permissions::with_read_view(::foundation::permissions::mint_permission_root<Region>(),
                                                          [](auto const& view) noexcept { return &view; });
    (void)lent;
    return 0;
}
