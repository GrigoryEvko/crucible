// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A named source of a region with an effectful row fails two rules: it is
// named, and the door reads no context.  The deleted twin, which tells the
// caller to pass the source with std::move, carries the row gate.  So the
// row gate refuses the call, and the diagnostic names the rule that the
// caller must fix first.  Without the row gate on the twin, the twin takes
// the call, and std::move would only lead to the row gate.
//
// Expected diagnostic: no matching function for with_read_view, whose
// candidates were discarded because ReadViewNeedsNoCtx is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace neg_read_view_named_effectful_source {

struct MappedRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using IoCtx = ::foundation::effects::detail::ctx_witnesses::BgIoWitness;

}  // namespace neg_read_view_named_effectful_source

int main() {
    using neg_read_view_named_effectful_source::IoCtx;
    using neg_read_view_named_effectful_source::MappedRegion;
    auto owned =
        ::foundation::permissions::mint_permission_root<MappedRegion>(IoCtx{::foundation::effects::testing::bg()});
    ::foundation::permissions::with_read_view(owned, [](auto const&) noexcept {});
    return 0;
}
