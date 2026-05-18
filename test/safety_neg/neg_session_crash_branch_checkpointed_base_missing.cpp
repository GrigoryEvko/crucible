// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-012 — `all_offers_have_crash_branch<CheckpointedSession<B,
// R>, PeerTag>` was undefined pre-fix (forward-declared primary
// template at SessionCrash.h:553), making the crash-branch walker
// produce hard substitution failure (or worse: false-by-substitution
// in SFINAE contexts) for transactional / checkpointed protocols.
//
// Violation: `CheckpointedSession<BaseProto, RollbackProto>` where
// the BASE branch carries an `Offer<Recv<Msg, End>>` with NO
// Recv<Crash<UnreliablePeer>, _> arm — and the rollback branch is
// crash-clean.  Pre-fix, the walker could not enter BASE at all
// (substitution failure → vacuously-true), so the structural defect
// hid silently.  Post-fix the walker distributes the check
// CONJUNCTIVELY over both branches and fires
// `[CrashBranch_Missing_In_Tree]` at the missing-arm site.
//
// Pairs with `neg_session_crash_branch_checkpointed_rollback_missing.cpp`
// (rollback-branch defect, same trait, different branch) — together
// they pin both legs of the CheckpointedSession AND-fold.
//
// Expected diagnostic: [CrashBranch_Missing_In_Tree] / static
//                       assertion failed.

#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>

using namespace crucible::safety::proto;

namespace neg_a2_012_ckpt_base {
struct UnreliablePeer {};
struct Msg {};
struct Recovery {};
}  // namespace neg_a2_012_ckpt_base

int main() {
    namespace ns = neg_a2_012_ckpt_base;

    // BASE branch: Offer<> lacks the crash arm for UnreliablePeer.
    using BadBase = Offer<Recv<ns::Msg, End>>;

    // ROLLBACK branch: structurally crash-safe (no Offer<> at all).
    using GoodRollback = End;

    using IllFormedProto = CheckpointedSession<BadBase, GoodRollback>;

    // Pre-fix: substitution failure on the missing trait specialisation
    // for CheckpointedSession produced false-by-default.  Post-fix:
    // the conjunctive walk recurses into BadBase and fires the
    // routed [CrashBranch_Missing_In_Tree] diagnostic.
    assert_every_offer_has_crash_branch_for<IllFormedProto,
                                            ns::UnreliablePeer>();
    return 0;
}
