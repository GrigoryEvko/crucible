// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A share of a region needs the row of its tag, or nothing says which
// contexts may read it.  No mint makes a token for a tag with no row,
// but the token type is spellable, so a function can take one as a
// parameter.  The fit concept of the token form of the share compares no
// row, so the row check in its body is what refuses the share.
//
// Expected diagnostic: the static_assert in mint_permission_share that
// asks for a row.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct RowlessRegion {};

[[maybe_unused]] void share_a_rowless_region(::foundation::permissions::Permission<RowlessRegion>&& token) {
    [[maybe_unused]] auto share = ::foundation::permissions::mint_permission_share(std::move(token));
}
}  // namespace

int main() {
    return 0;
}
