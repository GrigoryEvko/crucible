// The relation from tag to effect row is closed: a tag with no edge
// and no permission_row member has no row, and the root mint refuses
// it with the assertion that names the fix.  A primary template that
// answered the empty row would read the same tag as a pure tag, and the
// foreground could own its region.

#include <foundation/permissions/Permission.h>

namespace {
struct Undeclared {};
}  // namespace

int main() {
    [[maybe_unused]] auto token = ::foundation::permissions::mint_permission_root<Undeclared>();
    return 0;
}
