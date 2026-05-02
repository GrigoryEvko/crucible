// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14) + #861 reshape: mint_substrate_session
// rejects substrate-residency mismatch identically to mint_endpoint.
// This fixture pins the bridge's gate alongside the endpoint's gate
// (both use the same SubstrateFitsCtxResidency concept; both must
// fire on per-call WS mismatch).
//
// Pre-#861 this used SPSC<int, 1M> (4 MB total).  No longer a
// violation — per-call WS for SPSC<int, anything> = 192 B << L1d.
// The reshape uses a 64-KB cell so per-call WS legitimately
// overflows L1d.
//
// Violation: SPSC<Big{64 KB}, 4>.  Per-call WS ≥ 64 KB > 32 KB
// L1d.  HotFgCtx (L1Resident) → fit fails.

#include <crucible/concurrent/SubstrateSessionBridge.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf  = crucible::safety;

struct UserTag {};

// 64-KB cell.
struct alignas(64) Big {
    char buf[64 * 1024];
    auto operator<=>(Big const&) const = default;
};

int main() {
    using BigCellSpsc = conc::PermissionedSpscChannel<Big, 4, UserTag>;

    BigCellSpsc ch;
    auto whole = saf::mint_permission_root<conc::spsc_tag::Whole<UserTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UserTag>,
        conc::spsc_tag::Consumer<UserTag>>(std::move(whole));
    auto handle = ch.producer(std::move(pp));

    eff::HotFgCtx fg;
    auto bad = conc::mint_substrate_session<BigCellSpsc, conc::Direction::Producer>(
        fg, handle);  // SubstrateFitsCtxResidency fails on per-call WS
    (void)bad;
    (void)cp;
    return 0;
}
