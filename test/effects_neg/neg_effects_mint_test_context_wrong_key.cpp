// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-019 fixture for the CanMintTestContext concept gate.
// mint_test_context is now a function template gated on
// `requires CanMintTestContext<Key>` (Capabilities.h §XXI).  Passing
// bg_key (the wrong context's passkey) must trip the concept's
// `std::same_as<Key, detail::ctx_mint::test_key>` clause — distinct
// from the forge-key (private-ctor) and keyless (missing-arg) fixtures.
//
// Expected diagnostic: "constraints not satisfied" /
// "CanMintTestContext" / "same_as" / "bg_key" / "test_key" / "is private".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    auto t = eff::mint_test_context<eff::detail::ctx_mint::bg_key>(
        eff::detail::ctx_mint::bg_key{});
    (void)t;
    return 0;
}
