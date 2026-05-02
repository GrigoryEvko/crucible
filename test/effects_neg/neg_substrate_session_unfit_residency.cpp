// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14): mint_substrate_session<Substr, Dir>
// (ctx, h) rejects substrate-residency mismatch identically to
// mint_endpoint.  This fixture pins the bridge's gate alongside the
// endpoint's gate (both use the same SubstrateFitsCtxResidency
// concept; both must fire on residency mismatch).
//
// Violation: 4 MB SPSC + HotFgCtx (L1Resident) → fit fails.

#include <crucible/concurrent/SubstrateSessionBridge.h>

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
    auto bad = conc::mint_substrate_session<LargeSpsc, conc::Direction::Producer>(
        fg, handle);  // SubstrateFitsCtxResidency fails
    (void)bad;
    (void)cp;
    return 0;
}
