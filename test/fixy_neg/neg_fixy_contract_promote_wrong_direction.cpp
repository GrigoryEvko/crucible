// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture #1: mint_promote via fixy::contract::cipher
// alias rejects when the requested promotion runs DOWN the CipherTier
// chain (Hot → Cold).
//
// Violation: `mint_promote<From, To>(...)` carries a
// `requires PromotableTier<From, To, T>` clause where
// `PromotableTier = can_promote_tier_v<From, To> && std::move_constructible<T>`.
// `can_promote_tier_v` reduces to `tier_replaces(To, From)`, which is
// false when To is weaker than From (Cold is weaker than Hot).
// Routing through `fixy::contract::cipher::mint_promote` must reject
// identically to the substrate.
//
// Expected diagnostic: "PromotableTier" or "constraints not satisfied"
// — the requires-clause names the admission concept.

#include <crucible/fixy/Contract.h>

int main() {
    namespace fcc = ::crucible::fixy::contract::cipher;
    using Tier    = ::crucible::safety::CipherTierTag_v;

    // Start with a Hot tier handle and attempt to "promote" to Cold —
    // wrong direction.  can_promote_tier_v<Hot, Cold> == false.
    fcc::CipherTier<Tier::Hot, int> hot{42};
    [[maybe_unused]] auto bad =
        fcc::mint_promote<Tier::Hot, Tier::Cold>(std::move(hot));
    return 0;
}
