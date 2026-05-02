// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14): mint_endpoint<Substr, Dir>(ctx, h)
// rejects a HotFgCtx (L1Resident) paired with a substrate whose
// channel_byte_footprint exceeds the conservative L1d bound (32 KB).
//
// Violation: SPSC<int, 1024 * 1024> = 4 MB.  HotFgCtx claims
// L1Resident.  4 MB ≫ 32 KB; SubstrateFitsCtxResidency fails.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency.

#include <crucible/concurrent/Endpoint.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf  = crucible::safety;

struct UserTag {};

int main() {
    using LargeSpsc = conc::PermissionedSpscChannel<int, 1024 * 1024, UserTag>;

    LargeSpsc ch;
    auto whole = saf::mint_permission_root<conc::spsc_tag::Whole<UserTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UserTag>,
        conc::spsc_tag::Consumer<UserTag>>(std::move(whole));
    auto handle = ch.producer(std::move(pp));

    eff::HotFgCtx fg;
    auto bad = conc::mint_endpoint<LargeSpsc, conc::Direction::Producer>(fg, handle);
    (void)bad;
    (void)cp;
    return 0;
}
