// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscheckpoint::assert_checkpointed_matches<P, B, R>()`
// where P IS a CheckpointedSession but its base branch differs from
// ExpectedBase.  The SECOND of three static_asserts inside
// `assert_checkpointed_matches` fires with the diagnostic "base
// branch does not match".
//
// FIXY-V-061 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_checkpoint_not_a_checkpoint.cpp
//      (P is NOT a CheckpointedSession at all)
//   3. neg_fixy_sess_checkpoint_wrong_rollback_branch.cpp
//      (P IS checkpoint but rollback branch mismatched)
//
// This fixture's role: pin the "right shape, wrong commit-path"
// regression class.  A refactor that swaps the protocol on the base
// arm of an existing CheckpointedSession (e.g., changing the commit
// payload type, adding an extra Send, or removing an expected Recv)
// would otherwise pass a shape-only check.  The base-branch
// mismatch is distinct from the rollback-branch mismatch (fixture 3)
// because they emit different diagnostics — reviewers can identify
// the specific arm at fault without reading the call site.

#include <crucible/fixy/SessCheckpoint.h>
#include <crucible/sessions/Session.h>

namespace fscheckpoint = ::crucible::fixy::sess::checkpoint;
namespace proto        = ::crucible::safety::proto;

namespace {
struct Req {};
struct Resp {};
struct Err {};

// Two distinct base branches — declared and actual differ.
using ActualBase   = proto::Send<Req, proto::Recv<Resp, proto::End>>;
using ExpectedBase = proto::Send<Req, proto::End>;   // ≠ ActualBase
using RollbackPath = proto::Send<Req, proto::Recv<Err, proto::End>>;

// P IS a CheckpointedSession — shape check passes — but its base
// branch is ActualBase, not ExpectedBase.
using CkptSession = proto::CheckpointedSession<ActualBase, RollbackPath>;
}  // namespace

int main() {
    // Should FAIL: CkptSession's base branch is ActualBase, not
    // ExpectedBase.  The SECOND static_assert inside
    // assert_checkpointed_matches fires with the diagnostic
    // "base branch does not match".
    fscheckpoint::assert_checkpointed_matches<
        CkptSession, ExpectedBase, RollbackPath>();
    return 0;
}
