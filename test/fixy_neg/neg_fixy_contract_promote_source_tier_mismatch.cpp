// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture #1b (FIXY-U-074 HS14 round-out for
// fixy::contract::cipher::mint_promote):
// The mint signature is
//   `mint_promote(CipherTier<From, T> source)`
// with From bound by the user's template arguments.  Overload
// resolution requires the source argument's wrapper-tier to EQUAL
// the explicit From template parameter — there is no implicit
// cross-tier conversion on `CipherTier<>`.
//
// Distinct from fixture #1 (neg_fixy_contract_promote_wrong_direction):
//   * Fixture #1 supplies a `CipherTier<Hot, int>` source AND
//     explicitly writes `mint_promote<Hot, Cold>` — the source-vs-
//     From tier MATCHES, the chain-direction does NOT.  Rejection
//     fires on the `PromotableTier` requires-clause's
//     `can_promote_tier_v` predicate.
//   * Fixture #1b supplies a `CipherTier<Hot, int>` source but
//     explicitly writes `mint_promote<Cold, Warm>` — the source-vs-
//     From tier MISMATCHES (source is Hot, template says Cold).
//     Rejection fires on TEMPLATE ARGUMENT DEDUCTION /
//     OVERLOAD RESOLUTION, BEFORE the requires-clause is even
//     evaluated.
//
// Two distinct compile-rejection paths:
//   #1  — requires-clause predicate (`can_promote_tier_v`) fails.
//   #1b — function template overload-resolution / source-tier-vs-
//         param-tier mismatch (no matching `mint_promote` overload).
// HS14 is satisfied for the mint_promote factory.
//
// (Aside: same-tier "promotion" Hot→Hot is NOT a rejection — the
// chain-lattice `tier_replaces` relation is reflexive `≥`, so
// can_promote_tier_v<Hot, Hot> is true and the no-op is a defined
// identity.  Hence the source-tier-mismatch axis is the natural
// distinct class.)
//
// Expected diagnostic: no matching function for call | mint_promote |
//                      could not match | argument deduction failed.

#include <crucible/fixy/Contract.h>

int main() {
    namespace fcc = ::crucible::fixy::contract::cipher;
    using Tier    = ::crucible::safety::CipherTierTag_v;

    // Construct a Hot-tier handle.  The user then asks for a
    // Cold→Warm promotion — From=Cold is explicit, but the supplied
    // source is `CipherTier<Hot, int>`, not `CipherTier<Cold, int>`.
    // Overload resolution finds no matching `mint_promote` overload.
    fcc::CipherTier<Tier::Hot, int> hot{42};
    [[maybe_unused]] auto bad =
        fcc::mint_promote<Tier::Cold, Tier::Warm>(std::move(hot));
    return 0;
}
