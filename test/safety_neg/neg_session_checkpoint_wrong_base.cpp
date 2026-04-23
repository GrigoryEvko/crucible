// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_checkpointed_matches<P, ExpectedBase, ExpectedRollback>
// where P's actual base doesn't match ExpectedBase.  SessionCheckpoint.h's
// helper fires a classified static_assert.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionCheckpoint.h>

using namespace crucible::safety::proto;

struct Req {};
struct Resp {};
struct Err {};
struct DifferentBase {};

using CommitPath   = Send<Req, Recv<Resp, End>>;
using RollbackPath = Send<Req, Recv<Err, End>>;

using CkptProto = CheckpointedSession<CommitPath, RollbackPath>;

int main() {
    // Asserting CkptProto's base is DifferentBase — it's actually
    // CommitPath.  Fires the base-branch mismatch static_assert.
    assert_checkpointed_matches<CkptProto, DifferentBase, RollbackPath>();
    return 0;
}
