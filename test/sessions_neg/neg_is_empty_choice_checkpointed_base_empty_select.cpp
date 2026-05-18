// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-004 — HS14 fixture: `is_empty_choice<CheckpointedSession<B,
// R>>` MUST propagate empty Select<>/Offer<> defects from EITHER
// branch, symmetric to SessionDelegate.h:927-957's recursive
// specializations for the Delegate/Accept/Epoched* family that closed
// the fixy-CR-14 reachability hole.  Pre-fix CheckpointedSession had
// NO specialization, so the primary template at Session.h:534
// (`is_empty_choice<P>:std::false_type`) silently returned FALSE for
// every CheckpointedSession<B, R> regardless of inner-branch content
// — admitting `CheckpointedSession<Select<>, End>` past the mint
// gates at Session.h:2396 and PermissionedSession.h:1923 even though
// the base branch IS the ill-formed empty Select<>.  Post-fix the
// new specialization at SessionCheckpoint.h fires:
//
//     template <typename B, typename R>
//     struct is_empty_choice<CheckpointedSession<B, R>>
//         : std::bool_constant<is_empty_choice<B>::value ||
//                              is_empty_choice<R>::value> {};
//
// Base `B = Select<>` reports true_type via Session.h:539 → disjunction
// with R's false_type yields true_type → the wrapping CheckpointedSession
// correctly reports as defective.
//
// This fixture witnesses the BUGGY pre-fix classification by asserting
// the wrapping CheckpointedSession is NOT empty-choice.  Pre-fix the
// primary fires and the assertion holds (false_type → !false → true);
// post-fix the new specialization fires, disjunction returns true, the
// `!` makes the operand false, and the static_assert fires — the file
// no longer compiles.
//
// Why this matters: the empty-choice mint gates at Session.h:2396 and
// PermissionedSession.h:1923 are the SINGLE LOAD-BEARING reachability
// check that prevents `mint_session<Select<>, ...>` from compiling.
// Without the CheckpointedSession projection, a checkpointed wrapper
// disables that gate for ANY base or recovery branch — `mint_session<
// CheckpointedSession<Select<>, End>, R>` would silently mint a handle
// whose base branch can never make forward progress (no Select arms to
// pick), violating the Try(B) | RollbackTo(R) operational semantics.
//
// Companion to `neg_is_empty_choice_checkpointed_recovery_empty_offer.cpp`:
//   * That fixture covers the recovery (R) branch.
//   * THIS fixture covers the base (B) branch — empty Select<>.
// Together the pair pins fixy-A2-004's disjunctive-distribution
// promise: empty Choice in EITHER reachable branch is rejected.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCheckpoint.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

// Pre-fix witness: the primary template returns std::false_type for any
// unspecialized P, so `CheckpointedSession<Select<>, End>` silently
// classifies as non-empty-choice.  Asserting `!is_empty_choice_v<...>`
// holds pre-fix and MUST fail post-fixy-A2-004 — the new
// CheckpointedSession specialization fires, projects to true_type via
// disjunction with the base branch's empty Select<>.
static_assert(!proto::is_empty_choice_v<
    proto::CheckpointedSession<proto::Select<>, proto::End>>,
    "fixy-A2-004 regression: CheckpointedSession wraps an empty Select<> "
    "in its base branch; is_empty_choice must propagate the inner defect "
    "disjunctively, but the primary template silently returned false.");

}  // namespace

int main() { return 0; }
