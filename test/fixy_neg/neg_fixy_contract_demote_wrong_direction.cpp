// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture #2: mint_demote via fixy::contract::cipher
// alias rejects when the requested demotion runs UP the CipherTier
// chain (Cold → Hot).
//
// Violation: `mint_demote<From, To>(...)` carries a
// `requires DemotableTier<From, To, T>` clause where
// `DemotableTier = can_demote_tier_v<From, To> && std::move_constructible<T>`.
// `can_demote_tier_v` reduces to `tier_replaces(From, To)`, which is
// false when From is weaker than To (Cold is weaker than Hot).
// Routing through `fixy::contract::cipher::mint_demote` must reject
// identically to the substrate.
//
// Expected diagnostic: "DemotableTier" or "constraints not satisfied"
// — the requires-clause names the admission concept.

#include <crucible/fixy/Contract.h>

int main() {
    namespace fcc = ::crucible::fixy::contract::cipher;
    using Tier    = ::crucible::safety::CipherTierTag_v;

    // Start with a Cold tier handle and attempt to "demote" to Hot —
    // wrong direction (this is the promote path, but spelled as demote).
    // can_demote_tier_v<Cold, Hot> == false.
    fcc::CipherTier<Tier::Cold, int> cold{42};
    [[maybe_unused]] auto bad =
        fcc::mint_demote<Tier::Cold, Tier::Hot>(std::move(cold));
    return 0;
}
