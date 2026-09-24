// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// with_read_view keeps its source in its own frame while the body runs.
// A source passed by name stays with the caller, so a body could move it
// or end it through a capture while the view lives.  The deleted twin
// refuses a named source with the reason.
//
// Expected diagnostic: use of the deleted twin of with_read_view.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto owned = ::foundation::permissions::mint_permission_root<Region>();
    ::foundation::permissions::with_read_view(owned, [](auto const&) noexcept {});
    return 0;
}
