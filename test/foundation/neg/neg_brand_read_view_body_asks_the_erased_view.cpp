// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A branded source lends a view of its own brand.  A body that asks for
// the erased view asks for a proof about any region of the tag, which the
// source cannot give.  The door refuses the body.
//
// Expected diagnostic: no with_read_view matches, because ReadViewBody is
// not satisfied for the branded source.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    namespace fp = ::foundation::permissions;
    auto token = fp::mint_permission_root<Region>();
    auto back = fp::with_read_view(std::move(token), [](fp::ReadView<Region> const&) noexcept {});
    fp::permission_drop(std::move(back));
    return 0;
}
