// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 cross-tree audit (#858): SubstrateFitsCtxResidency rejects
// an L2-claiming Ctx paired with a substrate that requires L3 or
// beyond.
//
// Violation: BgDrainCtx is L2Resident.  SPSC<int, 1M> = 4 MB
// requires L3 per substrate_required_tier_v.  The fit check fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency.

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
    eff::BgDrainCtx bg;     // L2Resident — too tight for 4 MB
    requires_fit(*big, bg);
    return 0;
}
