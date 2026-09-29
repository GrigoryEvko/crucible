// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CtxFitsHardeningMint refuses the hot foreground context, because its
// row is empty and holds no Init.  Hardening::apply() issues blocking
// privileged system calls: mlock can fault in memory, and
// prctl(PR_SET_THP_DISABLE) changes process-wide policy.  From the hot
// recording thread they would stall the recording loop.
//
// The background fixture beside this one also fails on the missing Init,
// but through a different capability source, so the two cover distinct
// paths.
//
// Expected diagnostic: constraints not satisfied, naming
// CtxFitsHardeningMint or CtxOwnsCapability.

#include <crucible/warden/Hardening.h>

namespace neg_fixy_v_180_hardening_hot_fg {

[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::warden::mint_hardening(::foundation::effects::testing::foreground(), ::crucible::warden::Policy{});

}  // namespace neg_fixy_v_180_hardening_hot_fg

int main() { return 0; }
