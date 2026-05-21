// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-018 fixture for the CanMintInitContext concept gate.
// mint_init_context is now a function template gated on
// `requires CanMintInitContext<Key>` (Capabilities.h §XXI).  Passing
// bg_key (the wrong context's passkey) must trip the concept's
// `std::same_as<Key, detail::ctx_mint::init_key>` clause — distinct
// from the forge-key (private-ctor) and keyless (missing-arg) fixtures.
//
// Expected diagnostic: "constraints not satisfied" /
// "CanMintInitContext" / "same_as" / "bg_key" / "init_key" / "is private".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    auto init = eff::mint_init_context<eff::detail::ctx_mint::bg_key>(
        eff::detail::ctx_mint::bg_key{});
    (void)init;
    return 0;
}
