// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-5 (#856): IsL1ResidentCtx selective-dispatch witness.
//
// Violation: a function constrained on `IsL1ResidentCtx<Ctx>` only
// accepts contexts whose ctx_residency_tier resolves to L1Resident.
// BgDrainCtx (Resid=L2) fails the constraint; calling
// `requires_l1<BgDrainCtx>(...)` fires a clean concept-violation.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsL1ResidentCtx.

#include <crucible/concurrent/ExecCtxBridge.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;

template <eff::IsExecCtx Ctx>
    requires conc::IsL1ResidentCtx<Ctx>
constexpr void requires_l1(Ctx const&) noexcept {}

int main() {
    eff::BgDrainCtx bg;     // Residency = L2, not L1
    requires_l1(bg);
    return 0;
}
