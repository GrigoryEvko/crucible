// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-017 fixture for the CanMintBgContext concept gate.
// mint_bg_context is now a function template gated on
// `requires CanMintBgContext<Key>` (Capabilities.h §XXI).  Passing
// init_key (the wrong context's passkey) must trip the concept's
// `std::same_as<Key, detail::ctx_mint::bg_key>` clause — distinct
// mismatch class from neg_effects_mint_bg_context_forge_key.cpp
// (which exercises bg_key's private-ctor rejection at an
// unauthorized construction site) and from neg_effects_mint_bg_context_keyless.cpp
// (which exercises the missing-argument case).  Here the key is
// THE WRONG TYPE.
//
// To present an init_key value we go through the testing::TestWitness
// (its friend access lets us mint one), then immediately hand it to
// mint_bg_context — which must reject at concept-evaluation.
//
// Expected diagnostic: "constraints not satisfied" /
// "CanMintBgContext" / "same_as" / "init_key" / "bg_key".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    // Mint an init_key indirectly: testing::TestWitness's init() is
    // friended to construct one and immediately consume it to produce
    // an Init; we deliberately do NOT call that path.  Instead, the
    // intent here is purely demonstrative — we cannot construct a
    // detail::ctx_mint::init_key outside its friend list anyway, so
    // we route through the testing::TestWitness's existing accessor
    // and reach for the key via parameter type substitution.
    //
    // Simpler: just instantiate the template with the wrong key
    // type.  Default-constructing init_key fails outside friend
    // scope, but the template substitution itself is what we're
    // testing — concept rejects before instantiation succeeds.
    auto bg = eff::mint_bg_context<eff::detail::ctx_mint::init_key>(
        eff::detail::ctx_mint::init_key{});
    (void)bg;
    return 0;
}
