// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-012 — counterpart to
// `neg_session_crash_branch_checkpointed_base_missing.cpp`: the
// rollback-branch leg of the new CheckpointedSession crash-walker
// AND-fold.  Pre-fix the trait had no specialization for
// CheckpointedSession at all, so EITHER branch's defect hid
// silently.  Post-fix the walker distributes the check
// CONJUNCTIVELY — a missing crash arm in the rollback branch is
// just as much a verification defect as one in base.
//
// Worked rationale: rollback is REACHED at runtime via the
// `.rollback() &&`-qualified transition on
// SessionHandle<CheckpointedSession<...>> when application logic
// triggers retry (e.g., verify-reject closure in Cipher's
// tier-promotion replay loop).  If the rollback proto receives
// from an unreliable peer and lacks the Crash<Peer> arm, the
// runtime has no statically-discharged dispatch point for the
// peer's crash during retry — the canonical BSYZ22 verification
// gap this fixture pins.
//
// Pairs with the base-missing twin (root-level WF in the base
// branch).  Different rejection sites prove the AND-fold walks
// BOTH branches, not just the first one.
//
// Expected diagnostic: [CrashBranch_Missing_In_Tree] / static
//                       assertion failed.

#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

namespace neg_a2_012_ckpt_rollback {
struct UnreliablePeer {};
struct Msg {};
}  // namespace neg_a2_012_ckpt_rollback

int main() {
    namespace ns = neg_a2_012_ckpt_rollback;

    // BASE branch: structurally crash-safe (no Offer<> reachable).
    using GoodBase = End;

    // ROLLBACK branch: Offer<> lacks the crash arm.  Pre-fix this
    // hid behind the missing CheckpointedSession specialisation.
    using BadRollback = Offer<Recv<ns::Msg, End>>;

    using IllFormedProto = CheckpointedSession<GoodBase, BadRollback>;

    assert_every_offer_has_crash_branch_for<IllFormedProto,
                                            ns::UnreliablePeer>();
    return 0;
}
