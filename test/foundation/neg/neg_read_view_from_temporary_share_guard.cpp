// A read proof minted from a share guard that dies at the end of the
// statement.  The share ends with the guard, so the proof outlives the
// share it proves.  The deleted rvalue twin of mint_read_view is the
// better match for the expiring guard, so the call names a deleted
// function.  The same call on a named guard compiles.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    ::foundation::permissions::SharedPermissionPool pool{::foundation::permissions::mint_permission_root<Region>()};
    auto proof = ::foundation::permissions::mint_read_view(std::move(*pool.lend()));
    (void)proof;
    return 0;
}
