// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 cross-tree audit (#858): SubstrateFitsCtxResidency rejects
// an L1-claiming Ctx paired with a substrate whose footprint
// exceeds the conservative L1d bound (32 KB).
//
// Violation: HotFgCtx is L1Resident.  An SPSC<int, 1M> has a 4 MB
// footprint — orders of magnitude larger than L1d.  The
// SubstrateFitsCtxResidency requires-clause fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency / fits_in_tier_v.

#include <crucible/concurrent/SubstrateCtxFit.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;

struct UserTag {};

template <conc::IsSubstrate S, eff::IsExecCtx Ctx>
    requires conc::SubstrateFitsCtxResidency<S, Ctx>
constexpr void requires_fit(S const&, Ctx const&) noexcept {}

int main() {
    using LargeSpsc = conc::Substrate_t<conc::ChannelTopology::OneToOne,
                                         int, 1024 * 1024, UserTag>;
    LargeSpsc* big = nullptr;
    eff::HotFgCtx fg;
    requires_fit(*big, fg);  // 4 MB > 32 KB L1d
    return 0;
}
