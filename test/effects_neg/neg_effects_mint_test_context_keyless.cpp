// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #2 for effects::mint_test_context
// (Capabilities.h:438).  The factory's sole signature is
// `mint_test_context(detail::ctx_mint::test_key)` — the passkey
// parameter is MANDATORY.  Calling the factory with no argument
// must fail overload resolution: there is no keyless path to a Test
// context.
//
// Distinct mismatch class from
// neg_effects_mint_test_context_forge_key.cpp (#1): there a key is
// named but its private default ctor cannot be called; here NO key
// is presented, so the call has too few arguments.
//
// Expected diagnostic: "no matching function for call" /
// "too few arguments" / "mint_test_context".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    auto t = eff::mint_test_context();
    (void)t;
    return 0;
}
