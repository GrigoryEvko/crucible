// The relation from tag to effect row is closed: a tag with no edge
// and no permission_row member has no row, and the root mint refuses
// it with the assertion that names the fix.  Under the primary
// template this replaced, the same tag read as a pure tag and its
// region could be owned by the foreground.

#include <foundation/permissions/Permission.h>

namespace {
struct Undeclared {};
}  // namespace

int main() {
    [[maybe_unused]] auto token = ::foundation::permissions::mint_permission_root<Undeclared>();
    return 0;
}
