// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #1 for effects::mint_bg_context
// (Capabilities.h:422).  Unlike neg_effects_bg_default_ctor_private
// (which constructs `eff::Bg{}` directly), this fixture genuinely
// CALLS the mint factory — exercising its passkey gate.
//
// `mint_bg_context(detail::ctx_mint::bg_key)` is the only path to a
// Bg context.  The passkey `bg_key` has a PRIVATE default ctor,
// friended only to BackgroundThread + effects::testing::TestWitness.
// An unprivileged TU that tries to forge a key via `bg_key{}` and
// hand it to the factory must fail to compile — the key is
// unforgeable.
//
// Distinct mismatch class from
// neg_effects_mint_bg_context_keyless.cpp (#2): there the factory is
// called with NO key (the parameter is mandatory → overload
// resolution fails); here a key IS named but cannot be CONSTRUCTED
// (its default ctor is private to non-friends).
//
// Expected diagnostic: "is private within this context" /
// "private member" / "bg_key".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    // `bg_key{}` invokes the private default ctor from a non-friended
    // TU — this is the unforgeability seal.
    auto bg = eff::mint_bg_context(eff::detail::ctx_mint::bg_key{});
    (void)bg;
    return 0;
}
