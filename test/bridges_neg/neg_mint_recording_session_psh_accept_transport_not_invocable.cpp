// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006c: HS14 floor #1/2 for the new
// RecordingPermissionedSessionHandle<
//     Accept<DelegatedSession<InnerProto, InnerPS>, K>,
//     PS, Resource, LoopCtx>
// specialization shipped at RecordingPermissionedSessionHandle.h:812.
//
// The new spec's `accept(...)` method carries
//     requires std::is_invocable_v<Transport, Resource&>
//
// This fixture targets the REQUIRES-CLAUSE layer: Transport is `int`
// — not callable at all.  The requires clause rejects substitution
// before the method body instantiates.
//
// Distinct mismatch class from fixture #2 (transport_returns_void):
// #1 exercises the REQUIRES-CLAUSE body (`is_invocable_v` is false);
// #2 exercises the BODY-INSTANTIATION layer (`is_invocable_v` passes
// but the deduced `DelegatedResource = void` makes the inner PSH's
// body fail to declare a void variable).  Both witness the new spec's
// .accept() soundness gate, at distinct layers.
//
// Expected diagnostic:
//   "constraints not satisfied" / "is_invocable" /
//   "no matching function for call to ... accept" / "int"

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_mint_recording_session_psh_accept_transport_not_invocable {

struct CarrierChannel { int unused = 0; };

// Inner protocol is non-terminal so the inner PSH's static_assert on
// terminal-with-nonempty-PS does not pre-empt the requires-clause gate.
using InnerProto = proto::Send<int, proto::End>;

using CarrierProto = proto::Accept<
    proto::DelegatedSession<InnerProto, proto::EmptyPermSet>,
    proto::End>;

}  // namespace neg_mint_recording_session_psh_accept_transport_not_invocable

int main() {
    using namespace
        neg_mint_recording_session_psh_accept_transport_not_invocable;

    eff::HotFgCtx ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the Accept state.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    // Mint the recording wrapper.  Construction is gate-free; the
    // requires clause fires only on .accept().
    auto recording =
        proto::mint_recording_session(std::move(carrier), log, self, peer);

    // The forbidden call: Transport = int is NOT invocable with
    // (CarrierChannel&).  Requires-clause body fails.
    int not_a_transport = 0;
    auto bad = std::move(recording).accept(not_a_transport);
    (void)bad;
    return 0;
}
