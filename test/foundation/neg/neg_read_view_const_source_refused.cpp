// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A const source cannot move into the frame of the door, so the door
// refuses it.  The refusal must come from the deleted twin, whose message
// tells the caller to pass the source with std::move.  The primary door
// asks for a source that is not const.  Without that condition both doors
// accept a const rvalue, and the call is ambiguous: still refused, but
// with a message that names no rule.
//
// Expected diagnostic: use of the deleted with_read_view, with its
// message about std::move.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace neg_read_view_const_source {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_read_view_const_source

int main() {
    using neg_read_view_const_source::Region;
    auto const owned = ::foundation::permissions::mint_permission_root<Region>();
    auto back = ::foundation::permissions::with_read_view(std::move(owned), [](auto const&) noexcept {});
    (void)back;
    return 0;
}
