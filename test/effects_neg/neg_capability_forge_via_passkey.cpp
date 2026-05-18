// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-013 (#1620): Capability's public ctor takes a cap_mint_key
// by value as proof-of-authority.  cap_mint_key's default ctor is
// PRIVATE; only the mint_cap function template is friended to call
// it.  An arbitrary TU attempting to forge a passkey via
// `cap_mint_key{}` directly must be rejected — that's the
// unforgeability seal.
//
// Violation: forging a CapMintKey passkey from outside mint_cap.
//
// Expected diagnostic: "is private within this context" / "private
// member function" / "constexpr cap_mint_key() noexcept" declared
// private — the user-side `cap_mint_key{}` call site fails because
// it is NOT the befriended mint_cap function body.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    // The class name cap_mint_key is intentionally accessible (so
    // Capability's passkey ctor parameter can be named in
    // documentation and SFINAE traits) — but the default ctor that
    // produces a value is private + friended only to mint_cap.  An
    // arbitrary user TU cannot manufacture one.
    eff::Capability<eff::Effect::Alloc, eff::Bg> c{eff::cap_mint_key{}};
    (void)c;
    return 0;
}
