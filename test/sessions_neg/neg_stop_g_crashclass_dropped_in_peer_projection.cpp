// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-001 — HS14 fixture: the global-level `StopG<Peer, C>`
// MUST thread BSYZ22's `CrashClass C` through Peer's own local
// projection.  Pre-fix, every projection dropped C and returned the
// bare `Stop` alias (which equals `Stop_g<CrashClass::Abort>`),
// silently collapsing the four-tier crash lattice (Abort > Throw >
// ErrorReturn > NoThrow) to a single tier at the projection point.
//
// This fixture witnesses the BUGGY behaviour:
//
//     static_assert(std::is_same_v<
//         project_t<G_throw, Bob>,                 //  G has StopG<Bob, Throw>
//         Send<Ping, Stop>>);                      //  Stop == Stop_g<Abort>
//
// Pre-fix the projection produced `Send<Ping, Stop>` (CrashClass
// collapsed), so the static_assert passed and the file compiled
// cleanly.  Post-fixy-A2-001 the projection threads C and produces
// `Send<Ping, Stop_g<CrashClass::Throw>>`, which is structurally
// distinct from `Stop_g<CrashClass::Abort>` (per the static_asserts
// at SessionGlobal.h §"Projection: StopG with explicit CrashClass"
// items (4): `Stop_g<Abort> != Stop_g<Throw>`).  The static_assert
// below therefore fires post-fix and the file no longer compiles.
//
// Together with `neg_stop_g_crashclass_dropped_in_role_projection.cpp`
// this fixture pins the type-system end-to-end:
//   * THIS fixture covers the Peer's-own-projection path
//     (ProjectImpl<StopG<Peer, C>, Peer, RootG>).
//   * The companion covers the interacting-non-Peer path
//     (ProjectImpl<StopG<Peer, C>, Role, RootG> with
//      has_interaction_between_v<RootG, Role, Peer> == true).
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

// Bob is the crashed peer; CrashClass is Throw (BSYZ22 tier 2).  Per
// fixy-A2-001 the projection for Bob threads Throw and yields
// `Send<Ping, Stop_g<Throw>>`.  Asserting equivalence to the bare
// `Stop` continuation (which equals `Stop_g<Abort>`) MUST fail
// post-fix.
using G_throw =
    proto::Transmission<Bob, Alice, Ping,
                        proto::StopG<Bob, proto::CrashClass::Throw>>;

// fixy-A2-001 regression witness — fires when Peer's projection drops
// the CrashClass NTTP and collapses to Stop_g<Abort>.
static_assert(std::is_same_v<
    proto::project_t<G_throw, Bob>,
    proto::Send<Ping, proto::Stop>>,
    "fixy-A2-001 regression: CrashClass dropped in Peer projection");

}  // namespace

int main() { return 0; }
