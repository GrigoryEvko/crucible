// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006e: HS14 floor #1/2 for the new
// RecordingPermissionedSessionHandle<
//     EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
//                   MinEpoch, MinGeneration>,
//     PS, Resource, LoopCtx>
// specialization.
//
// Mirror of fixy-A2-006c's transport_not_invocable fixture, but at
// the EpochedAccept spec rather than the plain Accept spec.  The new
// spec's `.accept(...)` carries
//   requires std::is_invocable_v<Transport, Resource&>
//
// This fixture exercises the REQUIRES-CLAUSE layer: Transport = `int`
// is not callable with (Resource&), so the requires clause fails
// before any body instantiation runs.  Distinct mismatch class from
// fixture #2 (transport_returns_void): #1 exercises the requires-
// clause body (compile-time concept fail); #2 exercises body
// instantiation downstream of a satisfied requires.
//
// Distinct from EpochedDelegate's analogue (fixy-A2-006d): the
// recipient-side gate is `_satisfies_v` (>=), not `_matches_v` (==).
// Both still inherit the epoch-match enforcement from the inner PSH
// at carrier construction time, so this fixture must pass that gate
// to reach the .accept() requires-clause.
//
// Expected diagnostic:
//   "no matching function for call to ... accept" /
//   "constraints not satisfied" / "is_invocable_v" / "Transport"

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_mint_recording_session_psh_epoched_accept_transport_not_invocable {

struct CarrierChannel { int unused = 0; };

using InnerProto = proto::Send<int, proto::End>;

using CarrierProto = proto::EpochedAccept<
    proto::DelegatedSession<InnerProto, proto::EmptyPermSet>,
    proto::End,
    /*MinEpoch=*/7,
    /*MinGeneration=*/2>;

// Recipient ctx whose LoopCtx satisfies the carrier's epoch floor.
// EpochedAccept's gate is satisfies-relation (>=), so equal works.
using EpochCtxType = proto::EpochExecCtx<7, 2, eff::HotFgCtx>;

}  // namespace neg_mint_recording_session_psh_epoched_accept_transport_not_invocable

int main() {
    using namespace
        neg_mint_recording_session_psh_epoched_accept_transport_not_invocable;

    EpochCtxType ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the EpochedAccept state.  Epoch gate
    // passes because ctx is EpochExecCtx<7, 2, ...> >= floor 7,2.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    // Mint the recording wrapper.  Construction is gate-free; the
    // requires-clause body fires on .accept().
    auto recording =
        proto::mint_recording_session(std::move(carrier), log, self, peer);

    // The forbidden call: Transport = int is NOT invocable with
    // (CarrierChannel&).  Requires-clause rejects.
    int not_a_transport = 0;
    auto bad = std::move(recording).accept(not_a_transport);
    (void)bad;
    return 0;
}
