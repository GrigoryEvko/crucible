// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-003 — HS14 fixture: `is_dual_involutive<Delegate<T, K>>` MUST
// propagate non-involution from T and K, symmetric to Session.h's
// existing Send/Recv/Select/Offer/Loop/VendorPinned specializations at
// Session.h:689-713.  Pre-fix Delegate had NO specialization, so the
// primary template at Session.h:687 (`is_dual_involutive<P>:std::true_type`)
// fired for every Delegate<...> regardless of inner-protocol involution
// — silently classifying `Delegate<Offer<Sender<R>, ...>, End>` as
// involutive even though `dual(Offer<Sender<R>, ...>)` drops the role
// tag per fixy-CR-11 at Session.h:707.  Post-fix the new specialization
// at SessionDelegate.h fires:
//
//     template <typename T, typename K>
//     struct is_dual_involutive<Delegate<T, K>>
//         : std::bool_constant<is_dual_involutive<T>::value &&
//                              is_dual_involutive<K>::value> {};
//
// Inner `T = Offer<Sender<RoleA>, Recv<int, End>>` reports false_type
// via the fixy-CR-11 specialization → conjunction with K's true_type
// yields false_type → `Delegate<Offer<Sender<RoleA>, ...>, End>`
// correctly reports NON-involutive.
//
// This fixture witnesses the BUGGY pre-fix classification by asserting
// the wrapping Delegate is classified as involutive.  Pre-fix the
// primary fires and the assertion holds; post-fix the new
// specialization fires, the conjunction returns false, and the
// static_assert fires — the file no longer compiles.
//
// Why this matters: `refines_self_and_double_dual_v` (#1597 fixy-A2-023
// future task; today at SessionPatterns.h:944-1007) gates on
// `is_dual_involutive_v<P>` to decide whether protocol P safely admits
// the self-and-double-dual subtype refinement.  Without the Delegate
// propagation, a delegated channel whose inner protocol is non-
// involutive silently passes the gate — admitting a protocol class
// that loses information under the duality round-trip, which is the
// exact algebraic flaw fixy-CR-11 records.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionDelegate.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

struct RoleA {};

// Pre-fix witness: the primary template returns std::true_type for any
// unspecialized P, so `Delegate<Offer<Sender<RoleA>, Recv<int, End>>,
// End>` silently classifies as involutive.  Asserting involution MUST
// fail post-fixy-A2-003 — the new Delegate specialization fires,
// projects to false_type via the inner Offer<Sender<...>, ...>
// non-involution from fixy-CR-11.
static_assert(proto::is_dual_involutive_v<
    proto::Delegate<
        proto::Offer<proto::Sender<RoleA>, proto::Recv<int, proto::End>>,
        proto::End>>,
    "fixy-A2-003 regression: Delegate wraps Sender-annotated Offer; "
    "involution must propagate inner non-involution, but the primary "
    "template silently returned true.");

}  // namespace

int main() { return 0; }
