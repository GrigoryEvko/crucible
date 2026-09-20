// The region mint reads its tag off the permission it consumes, so a
// region cannot be minted under one tag with a permission for another:
// the tag named at the call and the tag on the permission fail to
// unify, and no region is made.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};
struct OtherBuffer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    int storage[4] = {};
    auto region = ::fixy::mint_owned_region<int, Buffer>(
        storage, std::size_t{4}, ::foundation::permissions::mint_permission_root<OtherBuffer>());
    (void)region;
    return 0;
}
