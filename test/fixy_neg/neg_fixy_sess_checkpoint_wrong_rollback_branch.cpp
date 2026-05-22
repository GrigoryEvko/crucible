// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscheckpoint::assert_checkpointed_matches<P, B, R>()`
// where P IS a CheckpointedSession AND its base branch matches
// ExpectedBase, but its rollback branch differs from
// ExpectedRollback.  The THIRD of three static_asserts inside
// `assert_checkpointed_matches` fires with the diagnostic "rollback
// branch does not match".
//
// FIXY-V-061 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_checkpoint_not_a_checkpoint.cpp
//      (P is NOT a CheckpointedSession at all)
//   2. neg_fixy_sess_checkpoint_wrong_base_branch.cpp
//      (P IS checkpoint but base branch mismatched)
//
// This fixture's role: pin the "right shape AND right commit-path,
// wrong abort-path" regression class.  The most insidious defect of
// the three because the happy-path tests would pass — only the
// rollback branch (which by definition is rarely exercised at
// runtime) carries the defect.  A refactor that adjusts the abort
// protocol (e.g., changing the error reply type, adding a
// notification step on abort, removing a cleanup acknowledgment)
// would slip through type-shape AND base-branch coverage; this
// fixture's diagnostic identifies that the failure is on the
// rollback arm specifically.

#include <crucible/fixy/SessCheckpoint.h>
#include <crucible/sessions/Session.h>

namespace fscheckpoint = ::crucible::fixy::sess::checkpoint;
namespace proto        = ::crucible::safety::proto;

namespace {
struct Req {};
struct Resp {};
struct Err {};

using CommitPath = proto::Send<Req, proto::Recv<Resp, proto::End>>;

// Two distinct rollback branches — declared and actual differ.
using ActualRollback   = proto::Send<Req, proto::Recv<Err, proto::End>>;
using ExpectedRollback = proto::Send<Req, proto::End>;   // ≠ ActualRollback

// P IS a CheckpointedSession with correct base branch, but its
// rollback branch is ActualRollback, not ExpectedRollback.
using CkptSession = proto::CheckpointedSession<CommitPath, ActualRollback>;
}  // namespace

int main() {
    // Should FAIL: CkptSession's rollback branch is ActualRollback,
    // not ExpectedRollback.  The THIRD static_assert inside
    // assert_checkpointed_matches fires with the diagnostic
    // "rollback branch does not match".  Base branch and shape
    // check both pass.
    fscheckpoint::assert_checkpointed_matches<
        CkptSession, CommitPath, ExpectedRollback>();
    return 0;
}
