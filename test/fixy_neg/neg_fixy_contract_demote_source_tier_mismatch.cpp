// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture #2b (FIXY-U-074 HS14 round-out for
// fixy::contract::cipher::mint_demote):
// The mint signature is
//   `mint_demote(CipherTier<From, T> source)`
// with From bound by the user's template arguments.  Overload
// resolution requires the source argument's wrapper-tier to EQUAL
// the explicit From template parameter — there is no implicit
// cross-tier conversion on `CipherTier<>`.
//
// Distinct from fixture #2 (neg_fixy_contract_demote_wrong_direction):
//   * Fixture #2 supplies a `CipherTier<Cold, int>` source AND
//     explicitly writes `mint_demote<Cold, Hot>` — the source-vs-
//     From tier MATCHES, the chain-direction does NOT.  Rejection
//     fires on the `DemotableTier` requires-clause's
//     `can_demote_tier_v` predicate.
//   * Fixture #2b supplies a `CipherTier<Cold, int>` source but
//     explicitly writes `mint_demote<Hot, Warm>` — the source-vs-
//     From tier MISMATCHES (source is Cold, template says Hot).
//     Rejection fires on TEMPLATE ARGUMENT DEDUCTION /
//     OVERLOAD RESOLUTION, BEFORE the requires-clause is even
//     evaluated.
//
// Two distinct compile-rejection paths:
//   #2  — requires-clause predicate (`can_demote_tier_v`) fails.
//   #2b — function template overload-resolution / source-tier-vs-
//         param-tier mismatch (no matching `mint_demote` overload).
// HS14 is satisfied for the mint_demote factory.
//
// (Aside: same-tier "demotion" Cold→Cold is NOT a rejection — the
// chain-lattice `tier_replaces` relation is reflexive `≥`, so
// can_demote_tier_v<Cold, Cold> is true and the no-op is a defined
// identity.  Hence the source-tier-mismatch axis is the natural
// distinct class.)
//
// Expected diagnostic: no matching function for call | mint_demote |
//                      could not match | argument deduction failed.

#include <crucible/fixy/Contract.h>

int main() {
    namespace fcc = ::crucible::fixy::contract::cipher;
    using Tier    = ::crucible::safety::CipherTierTag_v;

    // Construct a Cold-tier handle.  The user then asks for a
    // Hot→Warm demotion — From=Hot is explicit, but the supplied
    // source is `CipherTier<Cold, int>`, not `CipherTier<Hot, int>`.
    // Overload resolution finds no matching `mint_demote` overload.
    fcc::CipherTier<Tier::Cold, int> cold{42};
    [[maybe_unused]] auto bad =
        fcc::mint_demote<Tier::Hot, Tier::Warm>(std::move(cold));
    return 0;
}
