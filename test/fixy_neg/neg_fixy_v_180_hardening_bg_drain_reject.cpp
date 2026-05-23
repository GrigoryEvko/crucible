// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-180 HS14 fixture #1 of 2 for warden/Hardening.h::mint_hardening:
// CtxFitsHardeningMint rejects BgDrainCtx because the bg-drain row carries
// Effect::Bg (background-drain capability) but NOT Effect::Init.
// Hardening::apply() issues privileged Linux syscalls (sched_setattr,
// mlock2, prctl, etc. per V-180's mint_hardening_syscall_grants
// declaration) that are process-wide startup-only mutations.  Running
// them from a bg-drain thread would race the warden's own pinning
// against other bg workers and silently corrupt the process posture.
//
// Mismatch axis: Bg-cap context, NOT Init.
//   Distinct from fixture #2 (HotFgCtx — Fg-cap, also NOT Init).  Both
//   are Init-rejections but exercise distinct effect-row failure paths
//   (Bg vs Fg) ⇒ HS14 floor satisfied.
//
// Expected diagnostic: CtxFitsHardeningMint / CtxOwnsCapability /
//                      Effect::Init / constraints not satisfied.

#include <crucible/warden/Hardening.h>

namespace neg_fixy_v_180_hardening_bg_drain {

namespace eff = ::crucible::effects;

// BgDrainCtx admits Bg + Alloc + IO + Block but NOT Init.  Engaging
// mint_hardening from a bg-drain thread would be a clear category
// error — the privileged syscall set belongs to Init.
[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::warden::mint_hardening(eff::BgDrainCtx{},
                                       ::crucible::warden::Policy{});

}  // namespace neg_fixy_v_180_hardening_bg_drain

int main() {
    return 0;
}
