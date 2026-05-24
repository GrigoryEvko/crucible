// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling JoinPolicy<WeakerTier, T>::relax<StricterTier>()
// when StricterTier > WeakerTier in the JoinPolicyLattice.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding JoinPolicy::relax<>() — specifically, a
// slip from `JoinPolicyLattice::leq(WeakerTier, Tier)` to a permissive
// form (e.g. `true` while debugging) — would silently let a DETACH
// value claim JOIN_ALL status, defeating the V-078..V-080 engagement-
// strictness contract.  A downstream consumer asking for JOIN_ALL-
// floor evidence (e.g. a region body that needs structural assurance
// every child completed) would accept a value the producer only
// detached, breaking the policy contract at the API boundary instead
// of in a future audit.
//
// The lattice direction: JOIN_ALL is at the TOP of the join-policy
// chain (FORGET ⊏ DETACH ⊏ ABANDON ⊏ CANCEL ⊏ WAIT_DEADLINE ⊏
// JOIN_ALL).  Going DOWN (JOIN_ALL → CANCEL → ABANDON → DETACH →
// FORGET) is allowed — a stricter engagement still satisfies a
// weaker requirement.  Going UP (DETACH → JOIN_ALL) is forbidden —
// the detached region did NOT meet the stricter wait-for-all
// requirement; no way to retroactively claim the stronger discipline
// from the weaker.
//
// HS14 #1 of 2 for V-079 — pairs with neg_join_policy_mint_wrong_arg
// for the 2-fixture floor across distinct mismatch classes:
//   1. relax-to-stricter (this): tier-subsumption violation.
//   2. mint-wrong-arg:           substrate constructibility gate.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on JoinPolicy::relax<>().

#include <crucible/safety/JoinPolicy.h>

using namespace crucible::safety;

int main() {
    // FIXY-FOUND-090 #2245: construct via mint_join_policy so the §XXI
    // inventory scanner counts this fixture toward HS14 — same lattice
    // direction failure, broader §XXI coverage.
    auto detached = mint_join_policy<JoinPolicy_v::DETACH, int>(42);

    // Should FAIL: relax<JOIN_ALL> on a DETACH-pinned wrapper.  The
    // requires-clause `JoinPolicyLattice::leq(JOIN_ALL, DETACH)` is
    // FALSE — JOIN_ALL is above DETACH — so the relax<> overload is
    // excluded.
    auto joined = std::move(detached).relax<JoinPolicy_v::JOIN_ALL>();
    return joined.peek();
}
