// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 cross-tree audit (#858) + #861 reshape: SubstrateFitsCtxResidency
// rejects an L2-claiming Ctx paired with a substrate whose PER-CALL
// WORKING SET requires L3 or beyond.
//
// Pre-#861 this used SPSC<int, 1M> + BgDrainCtx (L2Resident).  That
// pairing now passes — per_call_WS = 192 B regardless of N, fits L2
// trivially.  The reshape uses a 1-MB struct CELL: one cell exceeds
// the 256-KB conservative L2 bound on its own.
//
// Violation: BigBig{char buf[1 MB]} cell → per_call_working_set_v ≥
// 1 MB > 256 KB conservative L2 bound.  BgDrainCtx claims
// L2Resident.  SubstrateFitsCtxResidency fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency.

#include <crucible/concurrent/SubstrateCtxFit.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;

struct UserTag {};

// 1-MB cell — exceeds L2 (256 KB) on its own.
struct alignas(64) BigBig {
    char buf[1024 * 1024];
    auto operator<=>(BigBig const&) const = default;
};

template <conc::IsSubstrate S, eff::IsExecCtx Ctx>
    requires conc::SubstrateFitsCtxResidency<S, Ctx>
constexpr void requires_fit(S const&, Ctx const&) noexcept {}

int main() {
    using BigBigCellSpsc = conc::Substrate_t<conc::ChannelTopology::OneToOne,
                                              BigBig, 2, UserTag>;
    BigBigCellSpsc* huge = nullptr;
    eff::BgDrainCtx bg;     // L2Resident — too tight for 1-MB cell
    requires_fit(*huge, bg);
    return 0;
}
