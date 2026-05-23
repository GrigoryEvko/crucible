// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-180 HS14 fixture #2 of 2 for warden/Hardening.h::mint_hardening:
// CtxFitsHardeningMint rejects HotFgCtx because the hot-foreground row
// carries Effect::Fg (the recording-thread capability — pinned, hot,
// L1-resident) but NOT Effect::Init.  Hardening::apply() issues blocking
// privileged syscalls (mlock can fault HBM, prctl(PR_SET_THP_DISABLE)
// rewrites process-wide policy); engaging them from the hot fg thread
// would stall the recording loop's ~5 ns/op budget by orders of magnitude.
//
// Mismatch axis: Fg-cap context, NOT Init.
//   Distinct from fixture #1 (BgDrainCtx — Bg-cap, also NOT Init).  Both
//   are Init-rejections but exercise distinct effect-row failure paths
//   (Fg vs Bg) ⇒ HS14 floor satisfied.
//
// Expected diagnostic: CtxFitsHardeningMint / CtxOwnsCapability /
//                      Effect::Init / constraints not satisfied.

#include <crucible/warden/Hardening.h>

namespace neg_fixy_v_180_hardening_hot_fg {

namespace eff = ::crucible::effects;

// HotFgCtx admits Fg + nothing else (no Init, no Block).  Engaging
// mint_hardening from the hot recording thread would stall the
// ~5 ns/op TraceRing push budget — blocking syscalls don't belong on
// the hot path.
[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::warden::mint_hardening(eff::HotFgCtx{},
                                       ::crucible::warden::Policy{});

}  // namespace neg_fixy_v_180_hardening_hot_fg

int main() {
    return 0;
}
