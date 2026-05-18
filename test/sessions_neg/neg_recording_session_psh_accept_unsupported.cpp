// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006 Phase 1 companion to
// neg_recording_session_psh_delegate_unsupported.cpp — proves the
// SYMMETRIC Phase 2 deferral on the Accept side.  Delegate is the
// sender (the carrier sends the inner DelegatedSession down the wire);
// Accept is the receiver (the carrier reads the inner PSH out of the
// wire).  Both protocol heads belong to the delegation family that
// fixy-A2-006b/c/d/e will ship as Phase 2.
//
// Together with the Delegate fixture this pair pins the discipline:
// either side of the delegation protocol triggers the same incomplete-
// type diagnostic from the primary template forward declaration.  A
// regression that re-introduced silent acceptance on Accept (but not
// Delegate, or vice versa) would surface independently.
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

int main() {
    eff::HotFgCtx ctx{};
    CarrierTag carrier{};

    // Build an Accept-headed PSH: the recipient receives the inner
    // DelegatedSession from the carrier.
    auto carrier_psh = proto::mint_permissioned_session<
        proto::Accept<
            proto::DelegatedSession<proto::End, proto::EmptyPermSet>,
            proto::End>>(
        ctx, std::move(carrier));

    proto::SessionEventLog log;

    // Phase 1 — this MUST fail.  Phase 2 ships in fixy-A2-006c.
    [[maybe_unused]] auto rec = proto::mint_recording_session(
        std::move(carrier_psh),
        log,
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    return 0;
}
