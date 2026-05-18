// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-003 — HS14 fixture: `is_dual_involutive<CheckpointedSession<B,
// R>>` MUST propagate non-involution from BOTH the base protocol B and
// the recovery protocol R, symmetric to Session.h's existing
// Send/Recv/Select/Offer/Loop/VendorPinned specializations at
// Session.h:689-713.  Pre-fix CheckpointedSession had NO specialization,
// so the primary template at Session.h:687
// (`is_dual_involutive<P>:std::true_type`) fired for every
// CheckpointedSession<B, R> regardless of inner-protocol involution —
// silently classifying `CheckpointedSession<Offer<Sender<R>, ...>, End>`
// as involutive even though `dual(Offer<Sender<R>, ...>)` drops the
// role tag per fixy-CR-11 at Session.h:707.  Post-fix the new
// specialization at SessionCheckpoint.h fires:
//
//     template <typename B, typename R>
//     struct is_dual_involutive<CheckpointedSession<B, R>>
//         : std::bool_constant<is_dual_involutive<B>::value &&
//                              is_dual_involutive<R>::value> {};
//
// Base `B = Offer<Sender<RoleA>, Recv<int, End>>` reports false_type
// via the fixy-CR-11 specialization → conjunction with R's true_type
// yields false_type → `CheckpointedSession<Offer<Sender<RoleA>, ...>,
// End>` correctly reports NON-involutive.
//
// This fixture witnesses the BUGGY pre-fix classification by asserting
// the wrapping CheckpointedSession is classified as involutive.
// Pre-fix the primary fires and the assertion holds; post-fix the new
// specialization fires, the conjunction returns false, and the
// static_assert fires — the file no longer compiles.
//
// Why this matters: `dual_of<CheckpointedSession<B, R>>::type =
// CheckpointedSession<dual_of<B>, dual_of<R>>` independently dualizes
// both arms (SessionCheckpoint.h:165-171).  When B = Sender-annotated
// Offer, dualizing B drops the role tag (fixy-CR-11), so the
// round-trip `dual_of<dual_of<CheckpointedSession<Annotated, R>>>`
// yields `CheckpointedSession<Stripped, R> ≠ source`.  The
// `refines_self_and_double_dual_v` gate (SessionPatterns.h:944-1007)
// uses `is_dual_involutive_v` to refuse exactly this class of
// information-losing round-trip; without the propagation, a
// checkpointed channel over a Sender-annotated base silently passes.
//
// Companion to `neg_is_dual_involutive_delegate_wraps_sender_offer.cpp`:
//   * That fixture covers the Delegate / Accept / Epoched* family.
//   * THIS fixture covers `CheckpointedSession<B, R>` with the
//     additional both-branches conjunction soundness.
// Together the pair pins fixy-A2-003's promise that EVERY non-spec'd
// combinator distributes is_dual_involutive over its component
// protocols — closing the silent-true-for-unspecialized hole that
// admitted Sender-Offer-wrapped non-involutive protocols past the
// gate.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCheckpoint.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

struct RoleA {};

// Pre-fix witness: the primary template returns std::true_type for any
// unspecialized P, so `CheckpointedSession<Offer<Sender<RoleA>,
// Recv<int, End>>, End>` silently classifies as involutive.  Asserting
// involution MUST fail post-fixy-A2-003 — the new CheckpointedSession
// specialization fires, projects to false_type via the inner
// Offer<Sender<...>, ...> non-involution from fixy-CR-11.
static_assert(proto::is_dual_involutive_v<
    proto::CheckpointedSession<
        proto::Offer<proto::Sender<RoleA>, proto::Recv<int, proto::End>>,
        proto::End>>,
    "fixy-A2-003 regression: CheckpointedSession wraps Sender-annotated "
    "Offer in its base branch; involution must propagate inner non-"
    "involution through both branches, but the primary template "
    "silently returned true.");

}  // namespace

int main() { return 0; }
