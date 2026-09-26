// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CtxFitsHardeningMint refuses the background drain context, because its
// row holds Bg and not Init.  Hardening::apply() issues privileged system
// calls (sched_setattr, mlock2, prctl and the others that
// hardening_syscall_atoms lists), and each one changes process-wide state
// at start-up.  From a drain thread they would race the pinning of the
// warden against other background workers.
//
// The foreground fixture beside this one also fails on the missing Init,
// but through a different capability source, so the two cover distinct
// paths.
//
// Expected diagnostic: constraints not satisfied, naming
// CtxFitsHardeningMint or CtxOwnsCapability.

#include <crucible/warden/Hardening.h>

namespace neg_fixy_v_180_hardening_bg_drain {

[[maybe_unused]] constexpr auto bad_dispatch = ::crucible::warden::mint_hardening(
    ::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, ::crucible::warden::Policy{});

}  // namespace neg_fixy_v_180_hardening_bg_drain

int main() { return 0; }
