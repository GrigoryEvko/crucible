// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The second door onto the borrow.  with_read_view mints a view and runs
// a body with it, so gating only mint_read_view would leave the whole
// borrow reachable through the scoped form: a body that returns the view
// it was handed carries the proof out of the call.
//
// The gate is spelled on the scoped form too, and it is spelled first in
// the clause.  Order matters: the invocability conjunct names
// ReadView<Tag, Brand>, and a conjunction short-circuits, so a region
// refused by the row gate never instantiates the view type it would not
// be allowed to hold.
//
// Sibling of neg_read_view_effectful_row_without_ctx.cpp, which is the
// same region through the bare factory.  This fixture is about the door,
// not about the region.
//
// Expected diagnostic: no matching function for with_read_view, whose
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
    ::foundation::permissions::with_read_view(owned, [](auto) noexcept {});
    return 0;
}
