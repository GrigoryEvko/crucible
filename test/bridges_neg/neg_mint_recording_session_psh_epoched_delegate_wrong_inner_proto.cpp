// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006d: HS14 floor #1/2 for the new
// RecordingPermissionedSessionHandle<
//     EpochedDelegate<DelegatedSession<InnerProto, InnerPS>, K,
//                     MinEpoch, MinGeneration>,
//     PS, Resource, LoopCtx>
// specialization.
//
// Mirror of fixy-A2-006b's wrong_inner_proto fixture, but at the
// EpochedDelegate spec rather than the plain Delegate spec.  The new
// spec's `delegate(...)` binds the delegated handle parameter to
//     PermissionedSessionHandle<InnerProto, ActualInnerPS,
//                               DelegatedResource, DelegatedLoopCtx>&&
// where `InnerProto` is the carrier's class-fixed template parameter
// (NOT deducible from the argument).  A delegated handle whose
// protocol disagrees with the carrier's InnerProto fails parameter
// binding before any requires-clause body runs.
//
// Distinct mismatch class from fixture #2 (transport_not_invocable):
// #1 exercises the SIGNATURE shape (delegated-handle InnerProto type
// equality vs the carrier's class-fixed InnerProto); #2 exercises the
// REQUIRES conjunct (is_invocable_v<Transport, ...>).  Both witness
// the EpochedDelegate spec's .delegate() soundness gate, at distinct
// layers, distinct from the plain Delegate spec by the additional
// epoch-match gate on LoopCtx (inherited transparently from inner_).
//
// Expected diagnostic:
//   "no matching function for call to ... delegate" /
//   "cannot convert" / "PermissionedSessionHandle" / "Send" / "Recv"

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_mint_recording_session_psh_epoched_delegate_wrong_inner_proto {

struct CarrierChannel { int unused = 0; };
struct InnerChannel   { int unused = 0; };

// Carrier's InnerProto = Send<int, End>.
using CarrierInnerProto = proto::Send<int, proto::End>;
// Delegated handle's actual protocol = Recv<int, End> -- mismatched.
using MismatchedInnerProto = proto::Recv<int, proto::End>;

using CarrierProto = proto::EpochedDelegate<
    proto::DelegatedSession<CarrierInnerProto, proto::EmptyPermSet>,
    proto::End,
    /*MinEpoch=*/7,
    /*MinGeneration=*/2>;

// ctx whose LoopCtx matches the carrier's epoch/generation.
using EpochCtxType = proto::EpochExecCtx<7, 2, eff::HotFgCtx>;

static void transport_handoff(CarrierChannel&, InnerChannel&&) noexcept {}

}  // namespace neg_mint_recording_session_psh_epoched_delegate_wrong_inner_proto

int main() {
    using namespace
        neg_mint_recording_session_psh_epoched_delegate_wrong_inner_proto;

    EpochCtxType ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the EpochedDelegate state.  Epoch gate
    // passes because ctx is EpochExecCtx<7, 2, ...>.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    // Mint the recording wrapper -- constructs cleanly.
    auto recording =
        proto::mint_recording_session(std::move(carrier), log, self, peer);

    // Construct the delegated endpoint at the WRONG inner protocol.
    auto wrong_inner = proto::mint_permissioned_session<MismatchedInnerProto>(
        ctx, InnerChannel{});

    // The forbidden call: delegated handle protocol = Recv<int, End>
    // but carrier's class-fixed InnerProto = Send<int, End>.
    // Parameter type binding rejects.
    auto bad = std::move(recording).delegate(
        std::move(wrong_inner), transport_handoff);
    (void)bad;
    return 0;
}
