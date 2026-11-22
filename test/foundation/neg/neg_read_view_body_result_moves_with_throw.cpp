// Negative-compile test. This file must fail to compile.
//
// The door gives the result of the body and the source back in one Record.
// A part of a Record moves with no throw, so a move of the Record never
// stops after one part and leaves the source behind.  ReadViewResultFitsARecord
// refuses a result whose move can throw.
//
// Expected diagnostic: the static assertion in the door that names a value
// whose move or end can throw.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace read_view_throwing_result_fixture {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// A count whose move is not noexcept.
struct MovesWithThrow {
    int count = 0;

    MovesWithThrow() noexcept = default;
    MovesWithThrow(MovesWithThrow&& other) noexcept(false) : count{other.count} {}
};

}  // namespace read_view_throwing_result_fixture

int main() {
    using read_view_throwing_result_fixture::MovesWithThrow;
    using read_view_throwing_result_fixture::Region;
    auto lent = ::foundation::permissions::with_read_view(::foundation::permissions::mint_permission_root<Region>(),
                                                          [](auto const&) noexcept { return MovesWithThrow{}; });
    (void)lent;
    return 0;
}
