// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for foundation::permissions::mint_read_view.
//
// A view is a proof about a region, so minting one is bounded by who may
// hold that region.  permission_row names the effects a context must own
// to hold it, and a factory that reads no context can be sound only for
// the empty row.  SharedPermissionPool::lend states that rule for the
// pooled share of the very same region; this factory stated it nowhere.
//
// Measured before ReadViewNeedsNoCtx: the call below compiled, and
// handed back a borrow proof of a region whose row names IO to a scope
// that declared no context at all.  The permission itself is reachable
// only through the ctx-bound root mint, so the region's row was checked
// once and then dropped at the borrow.
//
// The ctx-bound path for an effectful region is
// SharedPermissionPool::lend(ctx), which already reads a context.  There
// is deliberately no ctx-bound overload of this factory.
//
// Distinct mismatch class from
// neg_read_view_undeclared_row_without_ctx.cpp (fixture 2): there the
// tag declares no row at all and is refused by omission; here the row is
// declared and names an effect.
//
// Expected diagnostic: no matching function for mint_read_view, whose
// candidate was discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {
// A region whose row names IO, so only an IO-admitting context holds it.
struct MappedRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using IoCtx = ::foundation::effects::detail::ctx_witnesses::BgIoWitness;
}  // namespace

int main() {
    auto owned =
        ::foundation::permissions::mint_permission_root<MappedRegion>(IoCtx{::foundation::effects::testing::bg()});
    [[maybe_unused]] auto proof = ::foundation::permissions::mint_read_view(owned);
    return 0;
}
