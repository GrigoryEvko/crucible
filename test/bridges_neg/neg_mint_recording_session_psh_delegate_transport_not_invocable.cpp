// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006b: HS14 floor #2/2 for the new
// RecordingPermissionedSessionHandle<
//     Delegate<DelegatedSession<InnerProto, InnerPS>, K>,
//     PS, Resource, LoopCtx>
// specialization shipped at RecordingPermissionedSessionHandle.h:721.
//
// The new spec's `delegate(...)` method carries
//     requires (!is_stop_v<InnerProto> &&
//               std::is_invocable_v<Transport, Resource&,
//                                   DelegatedResource&&>)
//
// This fixture targets the SECOND conjunct: InnerProto is a sound
// (non-Stop) protocol but Transport is `int` -- not callable with
// (Resource&, DelegatedResource&&).  After the !is_stop_v conjunct
// passes, std::is_invocable_v rejects.
//
// Distinct mismatch class from fixture #1 (stop_inner): #1 exercises
// the is_stop_v<InnerProto> conjunct (FIRST in the conjunction);
// #2 exercises the is_invocable_v<Transport, ...> conjunct (SECOND
// in the conjunction) AFTER the first succeeds.
//
// Expected diagnostic:
//   "no matching function for call to ... delegate" /
//   "constraints not satisfied" / "is_invocable_v" / "Transport"

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_mint_recording_session_psh_delegate_transport_not_invocable {

struct CarrierChannel { int unused = 0; };
struct InnerChannel   { int unused = 0; };

// Inner protocol is non-Stop, so the !is_stop_v conjunct passes.
using InnerProto = proto::Send<int, proto::End>;

// Carrier protocol: hand off an InnerProto-typed endpoint then End.
using CarrierProto = proto::Delegate<
    proto::DelegatedSession<InnerProto, proto::EmptyPermSet>,
    proto::End>;

}  // namespace neg_mint_recording_session_psh_delegate_transport_not_invocable

int main() {
    using namespace
        neg_mint_recording_session_psh_delegate_transport_not_invocable;

    eff::HotFgCtx ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the Delegate state.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    // Mint the recording wrapper.  Construction is gate-free; the
    // second conjunct fires only on .delegate().
    auto recording =
        proto::mint_recording_session(std::move(carrier), log, self, peer);

    // Construct the inner endpoint at the InnerProto state.
    auto inner = proto::mint_permissioned_session<InnerProto>(
        ctx, InnerChannel{});

    // The forbidden call: Transport = int is NOT invocable with
    // (CarrierChannel&, InnerChannel&&).  Requires-clause rejects.
    int not_a_transport = 0;
    auto bad = std::move(recording).delegate(
        std::move(inner), not_a_transport);
    (void)bad;
    return 0;
}
