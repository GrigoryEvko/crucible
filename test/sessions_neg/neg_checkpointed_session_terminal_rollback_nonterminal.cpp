// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-029 — HS14 fixture #2.  Symmetric to fixture #1.  Asserts
// that the parameterised `is_terminal_state<CheckpointedSession<B, R>>`
// specialization rejects asymmetric cases where the ROLLBACK branch
// carries non-trivial protocol work.
//
//   is_terminal_state_v<CheckpointedSession<End, Recv<int, End>>>
//     == false  (post-fix: rollback is non-terminal, AND-fold false)
//
// Asserting the inverted shape (`is_terminal_state_v` is TRUE for a
// non-twin-terminal `CheckpointedSession` on the rollback side) MUST
// fail at compile time post-fix.
//
// Different inner combinator chosen on each fixture to pin that the
// rejection is symmetric: fixture #1 uses Send, fixture #2 uses Recv.
// Together they witness the AND-fold is direction-agnostic — it
// rejects non-terminal work in EITHER arm regardless of which one
// carries the residual protocol step.
//
// Why this matters (CheckpointedSession trait symmetry):
//   Every other CheckpointedSession trait recurses through BOTH
//   branches symmetrically — is_well_formed, is_empty_choice,
//   is_dual_involutive, is_subtype_sync_structural,
//   all_offers_have_crash_branch.  Pre-fix is_terminal_state was the
//   sole outlier (answered false unconditionally for the whole
//   combinator).  Post-fix the parameterised spec restores symmetry
//   and this fixture witnesses the rollback-branch arm.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

// Rollback branch carries a Recv — non-terminal.  Base is End —
// terminal.  Conjunction of branch-terminality MUST be false post-fix.
using AsymmetricCkpt = proto::CheckpointedSession<
    proto::End,
    proto::Recv<int, proto::End>>;

// Asserting trait is TRUE for this shape MUST fail post-fix — the
// AND-fold over branches yields false because rollback is non-terminal.
static_assert(proto::is_terminal_state_v<AsymmetricCkpt>,
    "fixy-A2-029 regression: CheckpointedSession<End, Recv<...>> "
    "wrongly classified as terminal — rollback branch carries Recv");

}  // namespace

int main() { return 0; }
