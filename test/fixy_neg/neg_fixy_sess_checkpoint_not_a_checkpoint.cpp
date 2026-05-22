// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscheckpoint::assert_checkpointed_matches<P, B, R>()`
// where P is structurally NOT a CheckpointedSession at all.  Here we
// pass a Send<T, K> — a plain message protocol, NOT a checkpointed
// (base, rollback) pair.  The first of three static_asserts inside
// `assert_checkpointed_matches` fires with the "P is not a
// CheckpointedSession" diagnostic.
//
// FIXY-V-061 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_checkpoint_wrong_base_branch.cpp
//      (P IS checkpoint but base branch mismatched)
//   3. neg_fixy_sess_checkpoint_wrong_rollback_branch.cpp
//      (P IS checkpoint but rollback branch mismatched)
//
// This fixture's role: pin the "expected checkpoint, got plain
// protocol" regression class at the consteval assertion site.  A
// buggy refactor that swaps a CheckpointedSession for a Send /
// Send-then-Recv pair would otherwise be accepted by a less-rigorous
// shape check; assert_checkpointed_matches's first static_assert
// catches THIS specific defect and emits a distinct diagnostic that
// the caller can recognize without reading the call-site source.

#include <crucible/fixy/SessCheckpoint.h>
#include <crucible/sessions/Session.h>

namespace fscheckpoint = ::crucible::fixy::sess::checkpoint;
namespace proto        = ::crucible::safety::proto;

namespace {
struct Req {};
struct Resp {};

// WRONG: a plain Send<T, K> — not a CheckpointedSession at all.
// Lacks the (base, rollback) pair structure entirely.
using PlainProto = proto::Send<Req, proto::Recv<Resp, proto::End>>;

// The branches we'd assert against, IF PlainProto were a checkpoint.
using ExpectedBase     = proto::Send<Req, proto::Recv<Resp, proto::End>>;
using ExpectedRollback = proto::Send<Req, proto::End>;
}  // namespace

int main() {
    // Should FAIL: PlainProto is Send<...>, not CheckpointedSession.
    // The FIRST static_assert inside assert_checkpointed_matches
    // fires with the diagnostic "P is not a CheckpointedSession".
    fscheckpoint::assert_checkpointed_matches<
        PlainProto, ExpectedBase, ExpectedRollback>();
    return 0;
}
