// A borrow proof minted from a temporary permission outlives what it
// proves.  The deleted rvalue twin of mint_read_view is the better
// match for the prvalue, so the call names a deleted function.  This
// is the refusal by value category; the sibling fixture
// neg_brand_read_view_proves_another_region.cpp is the refusal by
// identity.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto proof = ::foundation::permissions::mint_read_view(::foundation::permissions::mint_permission_root<Region>());
    (void)proof;
    return 0;
}
