// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-029 — HS14 fixture #1.  Asserts that the parameterised
// `is_terminal_state<CheckpointedSession<B, R>>` specialization
// runs the AND-fold over BOTH branches and rejects asymmetric cases
// where the base branch carries non-trivial protocol work.
//
//   is_terminal_state_v<CheckpointedSession<Send<int, End>, End>>
//     == false  (post-fix: base is non-terminal, AND-fold yields false)
//     == ???    (pre-fix: primary template answered false ANYWAY for
//                every CheckpointedSession<...>, so this assertion
//                trivially passed — but the meaning was wrong: the
//                handle was rejected on abandonment even when both
//                branches were trivially closed)
//
// Asserting the inverted shape (`is_terminal_state_v` is TRUE for a
// non-twin-terminal `CheckpointedSession`) MUST fail at compile time
// post-fix.  The static_assert fires the diagnostic family below.
//
// Companion to `neg_checkpointed_session_terminal_rollback_nonterminal`:
//   * THIS fixture pins the BASE-branch arm of the AND-fold.
//   * The companion pins the ROLLBACK-branch arm.
// Together the pair pins fixy-A2-029's contract that branch-terminality
// is the conjunction of base AND rollback terminality — symmetric to
// the CheckpointedSession trait fold convention (is_well_formed,
// is_empty_choice, is_dual_involutive, is_subtype_sync,
// all_offers_have_crash_branch).
//
// Why this matters (abandonment-check soundness):
//   Pre-fix the destructor's abandonment check fired on every
//   CheckpointedSession<...> drop because the primary trait answered
//   false unconditionally.  Post-fix BOTH branches must be terminal
//   for the trait to admit clean drop — so a handle whose base carries
//   a Send still aborts on abandonment.  This fixture witnesses the
//   parameterised specialization preserves that rejection for
//   asymmetric cases.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

// Base branch carries a Send — non-terminal.  Rollback is End —
// terminal.  Conjunction of branch-terminality MUST be false post-fix.
using AsymmetricCkpt = proto::CheckpointedSession<
    proto::Send<int, proto::End>,
    proto::End>;

// Asserting trait is TRUE for this shape MUST fail post-fix — the
// AND-fold over branches yields false because base is non-terminal.
static_assert(proto::is_terminal_state_v<AsymmetricCkpt>,
    "fixy-A2-029 regression: CheckpointedSession<Send<...>, End> "
    "wrongly classified as terminal — base branch carries Send");

}  // namespace

int main() { return 0; }
