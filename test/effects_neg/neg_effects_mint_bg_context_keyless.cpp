// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #2 for effects::mint_bg_context
// (Capabilities.h:422).  The factory's sole signature is
// `mint_bg_context(detail::ctx_mint::bg_key)` — the passkey
// parameter is MANDATORY.  Calling the factory with no argument
// must fail overload resolution: there is no keyless path to a Bg
// context.
//
// Distinct mismatch class from
// neg_effects_mint_bg_context_forge_key.cpp (#1): there a key is
// named but its private default ctor cannot be called; here NO key
// is presented at all, so the call site has too few arguments.
//
// Expected diagnostic: "no matching function for call" /
// "too few arguments" / "mint_bg_context".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    // No passkey presented — the mandatory bg_key parameter is
    // missing, so overload resolution finds no candidate.
    auto bg = eff::mint_bg_context();
    (void)bg;
    return 0;
}
