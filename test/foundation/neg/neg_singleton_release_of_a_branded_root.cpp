// A root of mint_permission_root keeps its brand.  Only the singleton root
// of a tag is on the erased identity, and only that root goes back through
// release_singleton_root.  So no door takes a plain branded root to the
// erased identity, where it would be a second owner of the singleton root
// of the tag.

#include <foundation/permissions/Permission.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    ::foundation::permissions::release_singleton_root(::foundation::permissions::mint_permission_root<Region>());
    return 0;
}
