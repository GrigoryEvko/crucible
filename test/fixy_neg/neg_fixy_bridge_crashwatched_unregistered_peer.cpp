// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-02 — mint_crash_watched_session HS14 floor (≥2 fixtures).
//
// Distinct mismatch class from fixture #1
// (neg_fixy_bridge_crashwatched_nothrow.cpp):
//
//   Fixture #1 (NoThrow gate):
//     * Route:   CrashWatchedHandle CLASS TEMPLATE direct ctor.
//     * Gate:    `require_crash_watched_contract_<Proto, NoThrow>`
//                — unreliable-peer + NoThrow is a type contradiction.
//     * Peer:    `DeadPeer` HAS survivor_registry specialization.
//
//   Fixture #2 (this file, survivor-registry gate):
//     * Route:   `fixy::bridge::mint_crash_watched_session<PeerTag>`
//                MINT FACTORY call site — exercises the §XXI canonical
//                discovery surface, not the class ctor.
//     * Gate:    `require_crash_survivors_declared_<PeerTag>` — the
//                survivor_registry<PeerTag> specialization is missing.
//     * Peer:    `UnregisteredPeer` deliberately lacks the
//                survivor_registry specialization.
//
// Routes through fixy:: AND exercises a DIFFERENT body static_assert
// AND a DIFFERENT entry point than the existing fixture.  HS14
// witnesses that the mint factory's body static_assert
// `require_crash_survivors_declared_<PeerTag>` fires when the
// survivor registry hasn't been specialized at the peer tag
// declaration site.
//
// Pre-fixy-A2-026 the mint discipline did NOT explicitly require
// IsSessionHandle<H>; the surface only got the body static_assert as
// a discovery surface.  Post-fixy-A2-026 the mint factory ships an
// explicit `requires IsSessionHandle<H>` discovery clause AND retains
// both body static_asserts (crash-contract + survivor-registry).
// This fixture pins the survivor-registry gate against future
// regressions where someone might accidentally weaken the discipline
// to "PeerTag accepts anything" — the diagnostic would still fire on
// the substrate-side static_assert in PermissionInherit.h but the
// fixy-level HS14 floor would have rotted.
//
// Expected diagnostic: "CrashWatchedHandle requires
// mint_permission_inherit survivors for" / "survivor_registry" /
// "static assertion failed" pointing at PermissionInherit.h or
// CrashTransport.h:require_crash_survivors_declared_.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;
namespace eff     = ::crucible::effects;
using ::crucible::safety::OneShotFlag;

// PeerTag deliberately UNREGISTERED — no survivor_registry<UnregisteredPeer>
// specialization exists anywhere in the program.  This MUST trip the
// body static_assert.
struct UnregisteredPeer {};

struct Channel {};

int main() {
    using P = proto::Send<int, proto::End>;

    eff::HotFgCtx ctx{};
    OneShotFlag flag;

    // Construct a valid permissioned session handle.  The mint
    // factory path is what fires — building a valid PSH is incidental
    // to setting up the call site.
    auto psh = proto::mint_permissioned_session<P>(ctx, Channel{});

    // Reach into the fixy:: re-export of mint_crash_watched_session.
    // PeerTag = UnregisteredPeer has no survivor_registry — the body
    // `require_crash_survivors_declared_<PeerTag>()` static_assert
    // fires.
    [[maybe_unused]] auto bad =
        fbridge::mint_crash_watched_session<UnregisteredPeer>(
            std::move(psh), flag);

    return 0;
}
