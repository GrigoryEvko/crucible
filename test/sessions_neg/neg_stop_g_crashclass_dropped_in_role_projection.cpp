// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-001 — HS14 fixture: the global-level `StopG<Peer, C>` MUST
// thread BSYZ22's `CrashClass C` through EVERY interacting role's
// local projection, not just the Peer's own.  Per BHYZ23 / BSYZ22
// [GR-✂] crash propagation: a surviving role whose protocol
// intersects with Peer's terminates with crash-induced Stop_g<C>, NOT
// clean End AND NOT default-tier `Stop = Stop_g<Abort>`.
//
// This fixture witnesses the BUGGY behaviour ON THE NON-PEER PATH:
//
//     static_assert(std::is_same_v<
//         project_t<G_throw, Alice>,               //  Alice interacts with Bob
//         Recv<Ping, Stop>>);                      //  Stop == Stop_g<Abort>
//
// Pre-fix, the non-Peer interacting projection returned
// `Recv<Ping, Stop>` regardless of the global-level CrashClass (the
// GAPS-001 fix tightened End → Stop, but lost the per-tier
// information — fixy-A2-001 restores it).  Post-fix the projection
// threads `C` through the `has_interaction_between_v<RootG, Role,
// Peer>` branch and produces `Recv<Ping, Stop_g<Throw>>`, which is
// structurally distinct from `Stop_g<Abort>`.  The static_assert
// below therefore fires post-fix and the file no longer compiles.
//
// Companion to `neg_stop_g_crashclass_dropped_in_peer_projection.cpp`:
//   * That fixture covers `ProjectImpl<StopG<Peer, C>, Peer, RootG>`.
//   * THIS fixture covers
//     `ProjectImpl<StopG<Peer, C>, Role, RootG>` with
//     `has_interaction_between_v<RootG, Role, Peer> == true`.
// Together the pair pins fixy-A2-001's promise that CrashClass
// propagates END-TO-END through projection, not just on the crashed
// participant's side.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionGlobal.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

struct Bob {};
struct Alice {};
struct Ping {};

// Bob is the crashed peer; Alice interacts with Bob (sends Ping).
// CrashClass is Throw (BSYZ22 tier 2).  Per fixy-A2-001 the
// projection for the interacting non-Peer Alice threads Throw and
// yields `Recv<Ping, Stop_g<Throw>>`.  Asserting equivalence to the
// bare `Stop` continuation (which equals `Stop_g<Abort>`) MUST fail
// post-fix.
//
// (Direction note: `Transmission<Bob, Alice, Ping, ...>` means Bob
// sends Ping to Alice — so Alice's projection is `Recv<Ping, ...>`.
// Bob crashes AFTER the send; Alice did interact with Bob so the
// crash propagates into Alice's terminal.)
using G_throw =
    proto::Transmission<Bob, Alice, Ping,
                        proto::StopG<Bob, proto::CrashClass::Throw>>;

// fixy-A2-001 regression witness — fires when the non-Peer
// interacting projection drops the CrashClass NTTP and collapses to
// Stop_g<Abort>.
static_assert(std::is_same_v<
    proto::project_t<G_throw, Alice>,
    proto::Recv<Ping, proto::Stop>>,
    "fixy-A2-001 regression: CrashClass dropped in interacting-role projection");

}  // namespace

int main() { return 0; }
