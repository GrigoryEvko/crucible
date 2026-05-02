// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 1 cross-tree audit (#858) + #861 reshape: SubstrateFitsCtxResidency
// rejects an L1-claiming Ctx paired with a substrate whose PER-CALL
// WORKING SET exceeds the conservative L1d bound (32 KB).
//
// Pre-#861 this fixture used SPSC<int, 1M> (4 MB TOTAL storage).
// That's no longer a violation under the per-call WS gate — the
// hot-path producer touches only ~3 cache lines (192 B) per try_push
// regardless of capacity.  The reshape uses a 64-KB struct CELL,
// where one cell legitimately overflows L1d on its own.
//
// Violation: Big{char buf[64K]} cell → per_call_working_set_v ≥
// 64 KB > 32 KB conservative L1d bound.  HotFgCtx claims L1Resident.
// SubstrateFitsCtxResidency fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at SubstrateFitsCtxResidency / fits_in_tier_v on
// per_call_working_set_v<S> for L1Resident.

#include <crucible/concurrent/SubstrateCtxFit.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;

struct UserTag {};

// 64-KB cell — exceeds L1d (32 KB) on its own, so per-call WS
// exceeds L1d regardless of capacity.
struct alignas(64) Big {
    char buf[64 * 1024];
    auto operator<=>(Big const&) const = default;
};

template <conc::IsSubstrate S, eff::IsExecCtx Ctx>
    requires conc::SubstrateFitsCtxResidency<S, Ctx>
constexpr void requires_fit(S const&, Ctx const&) noexcept {}

int main() {
    using BigCellSpsc = conc::Substrate_t<conc::ChannelTopology::OneToOne,
                                           Big, 4, UserTag>;
    BigCellSpsc* big = nullptr;
    eff::HotFgCtx fg;
    requires_fit(*big, fg);  // 64 KB cell > 32 KB L1d
    return 0;
}
