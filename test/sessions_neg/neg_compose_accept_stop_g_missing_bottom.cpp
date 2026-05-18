// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-002 — HS14 fixture: `compose<Accept<Stop_g<C>, K>, Q>` MUST
// collapse to `Stop_g<C>` symmetric to the Delegate-side bottom-
// preservation at SessionDelegate.h:640-647.  Pre-fix the general
// `compose<Accept<T, K>, Q>` specialization fired even when T was
// `Stop_g<C>`, producing the structurally wrong
// `Accept<Stop_g<C>, compose<K, Q>>` — semantically claiming the
// recipient still advances K past an already-crashed delegated
// endpoint.  Post-fix the new specialization
// `compose<Accept<Stop_g<C>, K>, Q> -> compose<Stop_g<C>, Q> = Stop_g<C>`
// fires; the buggy `Accept<Stop_g<C>, ...>` shape disappears.
//
// This fixture witnesses the BUGGY pre-fix shape for plain
// `Accept<Stop_g<CrashClass::Throw>, K>`:
//
//     static_assert(std::is_same_v<
//         compose_t<Accept<Stop_g<Throw>, Send<int, End>>, Recv<Ack, End>>,
//         Accept<Stop_g<Throw>, compose_t<Send<int, End>, Recv<Ack, End>>>>);
//
// Pre-fix the assertion holds because the general Accept compose rule
// fires.  Post-fix the bottom-preservation rule wins partial-ordering
// (more specialized), produces `Stop_g<Throw>`, and the static_assert
// fires — the file no longer compiles.
//
// Companion to `neg_compose_epoched_accept_stop_g_missing_bottom.cpp`:
//   * THIS fixture covers `compose<Accept<Stop_g<C>, K>, Q>`.
//   * The companion covers
//     `compose<EpochedAccept<Stop_g<C>, K, MinEpoch, MinGen>, Q>`.
// Together the pair pins fixy-A2-002's promise that BOTH Accept
// shapes collapse to Stop_g<C> under composition — restoring the
// duality identity `dual(compose<Delegate<...>, Q>) ==
// compose<dual<Delegate<...>>, dual<Q>>` on every CrashClass tier.
//
// Why this matters (BSYZ22 / HYC 2008 duality coherence):
//   `dual_of(Delegate<T, K>) = Accept<T, dual(K)>` (Session.h via
//   SessionDelegate.h:590).  Without the symmetric bottom rule, the
//   composition operator stops commuting with duality on the Accept
//   arm — a load-bearing algebraic property the framework documents
//   at Session.h:730-732.  This fixture pins the symmetry as a
//   compile-time invariant by witnessing the pre-fix asymmetric
//   shape and asserting it post-fix-impossibility.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

struct Ack {};

// Pre-fix witness: the general `compose<Accept<T, K>, Q>` rule fires
// for T = Stop_g<Throw>, producing the structurally wrong
// `Accept<Stop_g<Throw>, compose<Send<int, End>, Recv<Ack, End>>>` =
// `Accept<Stop_g<Throw>, Send<int, Recv<Ack, End>>>`.  Asserting
// equivalence to that shape MUST fail post-fixy-A2-002 — the new
// bottom-preservation specialization fires and the type collapses
// to `Stop_g<Throw>`.
static_assert(std::is_same_v<
    proto::compose_t<
        proto::Accept<proto::Stop_g<proto::CrashClass::Throw>,
                      proto::Send<int, proto::End>>,
        proto::Recv<Ack, proto::End>>,
    proto::Accept<proto::Stop_g<proto::CrashClass::Throw>,
                  proto::compose_t<proto::Send<int, proto::End>,
                                   proto::Recv<Ack, proto::End>>>>,
    "fixy-A2-002 regression: Accept<Stop_g<C>, K> compose did not "
    "collapse to Stop_g<C>");

}  // namespace

int main() { return 0; }
