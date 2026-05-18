// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-004 — HS14 fixture: companion to
// `neg_is_empty_choice_checkpointed_base_empty_select.cpp`.  Witnesses
// the SYMMETRIC defect on the recovery (R) branch — empty Offer<>
// silently classified as non-empty-choice pre-fix because the primary
// template at Session.h:534 fired for the unspecialized
// CheckpointedSession.
//
// Post-fix the new specialization at SessionCheckpoint.h fires:
//
//     template <typename B, typename R>
//     struct is_empty_choice<CheckpointedSession<B, R>>
//         : std::bool_constant<is_empty_choice<B>::value ||
//                              is_empty_choice<R>::value> {};
//
// Recovery `R = Offer<>` reports true_type via Session.h:540 →
// disjunction with B's false_type yields true_type → the wrapping
// CheckpointedSession correctly reports as defective.  Pre-fix
// passes; post-fix the static_assert fires and the file fails to
// compile.
//
// Rationale: CheckpointedSession represents Try(B) | RollbackTo(R) —
// BOTH arms are reachable at runtime via rollback/abandon, so an
// empty Choice in EITHER branch fails the reachability invariant
// that the mint gates at Session.h:2396 and PermissionedSession.h:1923
// enforce.  Pre-fix a CheckpointedSession whose RECOVERY branch is
// `Offer<>` would silently mint, even though after rollback the
// participant has zero branches to offer — operationally a deadlock,
// definitionally a defect.
//
// Together with the base-branch fixture, this pair pins fixy-A2-004's
// disjunctive-distribution promise.  Both fixtures use a distinct
// empty-Choice variant (`Select<>` vs `Offer<>`) and a distinct
// placement (base vs recovery) so a regression that re-introduced
// the primary-template silent admission on EITHER side would surface
// independently.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCheckpoint.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

// Pre-fix witness: the primary template returns std::false_type for any
// unspecialized P, so `CheckpointedSession<End, Offer<>>` silently
// classifies as non-empty-choice.  Asserting `!is_empty_choice_v<...>`
// holds pre-fix and MUST fail post-fixy-A2-004 — the new
// CheckpointedSession specialization fires, projects to true_type via
// disjunction with the recovery branch's empty Offer<>.
static_assert(!proto::is_empty_choice_v<
    proto::CheckpointedSession<proto::End, proto::Offer<>>>,
    "fixy-A2-004 regression: CheckpointedSession wraps an empty Offer<> "
    "in its recovery branch; is_empty_choice must propagate the inner "
    "defect disjunctively, but the primary template silently returned "
    "false.");

}  // namespace

int main() { return 0; }
