// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006 Phase 1 deferral witness — mint_recording_session's PSH
// overload covers seven production-critical protocol heads (End /
// Stop_g / Send / Recv / Select / Offer / CheckpointedSession).  The
// delegation family (Delegate / Accept / EpochedDelegate /
// EpochedAccept) is deferred to fixy-A2-006b/c/d/e because the
// recipient-side inner-handle PSH carries its own PermSet and
// threading audit recording across the delegated-handle boundary is a
// separate design decision warranting its own audit pass.
//
// Until Phase 2 ships, wrapping a Delegate-headed PSH must compile-
// fail rather than silently dropping the recording behavior.  The
// primary template `RecordingPermissionedSessionHandle<Proto, PS, R, L>`
// is forward-declared but has no body for Delegate, so instantiating
// it triggers "invalid use of incomplete type" from the type system.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "incomplete type" / "no matching" / "invalid use".

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct CarrierTag {};
struct InnerTag {};

int main() {
    eff::HotFgCtx ctx{};
    CarrierTag carrier{};

    // Build a Delegate-headed PSH: the recipient must Send the
    // inner DelegatedSession down the carrier.
    auto carrier_psh = proto::mint_permissioned_session<
        proto::Delegate<
            proto::DelegatedSession<proto::End, proto::EmptyPermSet>,
            proto::End>>(
        ctx, std::move(carrier));

    proto::SessionEventLog log;

    // Phase 1 — this MUST fail.  Phase 2 ships in fixy-A2-006b.
    [[maybe_unused]] auto rec = proto::mint_recording_session(
        std::move(carrier_psh),
        log,
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    return 0;
}
