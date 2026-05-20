// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #1 for effects::mint_init_context
// (Capabilities.h:430).  The only path to an Init context is
// `mint_init_context(detail::ctx_mint::init_key)`.  The passkey
// `init_key` has a PRIVATE default ctor, friended only to Vigil,
// BackgroundThread, and effects::testing::TestWitness.  An
// unprivileged TU that forges a key via `init_key{}` and hands it to
// the factory must fail to compile.
//
// Distinct mismatch class from
// neg_effects_mint_init_context_keyless.cpp (#2): there the factory
// is called with NO key (overload resolution fails); here a key IS
// named but cannot be CONSTRUCTED (private default ctor).
//
// Expected diagnostic: "is private within this context" /
// "private member" / "init_key".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    auto init = eff::mint_init_context(eff::detail::ctx_mint::init_key{});
    (void)init;
    return 0;
}
