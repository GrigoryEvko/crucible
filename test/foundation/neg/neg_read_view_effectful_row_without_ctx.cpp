// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for the row gate of foundation::permissions::with_read_view.
//
// A view is a proof about a region, so a borrow is bounded by who may
// hold that region.  permission_row names the effects a context must own
// to hold it, and a door that reads no context is sound only for the
// empty row.  SharedPermissionPool::lend states that rule for the pooled
// share of the same region.  The ctx-bound path for an effectful region
// is SharedPermissionPool::lend(ctx), which reads a context.
//
// A different class from neg_read_view_undeclared_row_without_ctx.cpp:
// there the tag declares no row and is refused by omission.  Here the
// row is declared and names an effect.
//
// Expected diagnostic: no matching function for with_read_view, whose
// candidate was discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct MappedRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using IoCtx = ::foundation::effects::detail::ctx_witnesses::BgIoWitness;

}  // namespace

int main() {
    auto owned =
        ::foundation::permissions::mint_permission_root<MappedRegion>(IoCtx{::foundation::effects::testing::bg()});
    [[maybe_unused]] auto back =
        ::foundation::permissions::with_read_view(std::move(owned), [](auto const&) noexcept {});
    return 0;
}
