// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A std::function keeps its callable behind a pointer to void and a
// pointer to a function, so its type names no ReadView.  A body that
// returns one with the view captured by reference carries the view out
// of the frame of the door.  ReadViewResultStaysInside refuses a result
// that holds an untyped address.
//
// Expected diagnostic: the static assertion in the door that names the
// untyped address.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <functional>

namespace read_view_type_erased_fixture {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace read_view_type_erased_fixture

int main() {
    using read_view_type_erased_fixture::Region;
    auto lent = ::foundation::permissions::with_read_view(
        ::foundation::permissions::mint_permission_root<Region>(),
        [](auto const& view) { return std::function<void()>{[&view] { (void)view; }}; });
    (void)lent;
    return 0;
}
