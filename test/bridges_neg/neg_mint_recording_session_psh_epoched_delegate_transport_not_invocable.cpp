// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006d: HS14 floor #2/2 for the new
// RecordingPermissionedSessionHandle<
//     EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
//                     MinEpoch, MinGeneration>,
//     PS, Resource, LoopCtx>
// specialization.
//
// Mirror of fixy-A2-006b's transport_not_invocable fixture, but at
// the EpochedDelegate spec rather than the plain Delegate spec.  The
// new spec's `.delegate(...)` carries
//     requires (!is_stop_v<InnerProto> &&
//               std::is_invocable_v<Transport, Resource&,
//                                   DelegatedResource&&>)
//
// This fixture targets the SECOND conjunct: InnerProto is non-stop
// but Transport = `int` -- not callable with (Resource&,
// DelegatedResource&&).  Requires-clause body fails.
//
// Distinct mismatch class from fixture #1 (wrong_inner_proto): #1
// exercises the SIGNATURE-shape gate (parameter binding fails because
// InnerProto is non-deducible); #2 exercises the REQUIRES conjunct
// body.  Both witness the EpochedDelegate spec's soundness gate at
// distinct layers.
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

namespace neg_mint_recording_session_psh_epoched_delegate_transport_not_invocable {

struct CarrierChannel { int unused = 0; };
struct InnerChannel   { int unused = 0; };

// Inner protocol is non-Stop, so the !is_stop_v conjunct passes.
using InnerProto = proto::Send<int, proto::End>;

using CarrierProto = proto::EpochedDelegate<
    proto::DelegatedSession<InnerProto, proto::EmptyPermSet>,
    proto::End,
    /*MinEpoch=*/7,
    /*MinGeneration=*/2>;

// ctx whose LoopCtx matches the carrier's epoch/generation.
using EpochCtxType = proto::EpochExecCtx<7, 2, eff::HotFgCtx>;

}  // namespace neg_mint_recording_session_psh_epoched_delegate_transport_not_invocable

int main() {
    using namespace
        neg_mint_recording_session_psh_epoched_delegate_transport_not_invocable;

    EpochCtxType ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the EpochedDelegate state.  Epoch gate
    // passes because ctx is EpochExecCtx<7, 2, ...>.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    // Mint the recording wrapper.  Construction is gate-free; the
    // requires-clause body fires on .delegate().
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
