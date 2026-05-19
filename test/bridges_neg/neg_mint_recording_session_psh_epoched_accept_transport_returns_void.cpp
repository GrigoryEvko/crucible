// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006e: HS14 floor #2/2 for the new
// RecordingPermissionedSessionHandle<
//     EpochedAccept<DelegatedSession<InnerProto, InnerPS>, K,
//                   MinEpoch, MinGeneration>,
//     PS, Resource, LoopCtx>
// specialization.
//
// Mirror of fixy-A2-006c's transport_returns_void fixture, but at
// the EpochedAccept spec rather than the plain Accept spec.  The new
// spec's `accept(...)` defaults the second template parameter:
//   typename DelegatedResource = std::invoke_result_t<Transport, Resource&>
//
// When Transport returns `void`, deduction makes `DelegatedResource =
// void`.  The requires clause `is_invocable_v<Transport, Resource&>`
// PASSES (a void-returning callable IS invocable).  But the inner
// PSH's accept body cannot declare a variable of type `void`:
//   DelegatedResource delegated_res = std::invoke(transport, resource_);
// This is ill-formed at BODY INSTANTIATION, after the requires clause
// has been satisfied.
//
// Distinct mismatch class from fixture #1 (transport_not_invocable):
// #1 exercises the REQUIRES-CLAUSE body (compile-time concept fail);
// #2 exercises the BODY-INSTANTIATION layer (template body fails to
// instantiate downstream of a satisfied requires).  Both witness the
// EpochedAccept spec's .accept() soundness gate, at distinct layers,
// distinct from the plain Accept spec by the additional epoch-floor
// gate on LoopCtx (inherited transparently from inner_).
//
// Expected diagnostic:
//   "cannot declare variable" / "incomplete type 'void'" /
//   "void delegated_res" / "no matching function" / "accept"

#include <crucible/bridges/RecordingPermissionedSessionHandle.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_mint_recording_session_psh_epoched_accept_transport_returns_void {

struct CarrierChannel { int unused = 0; };

using InnerProto = proto::Send<int, proto::End>;

using CarrierProto = proto::EpochedAccept<
    proto::DelegatedSession<InnerProto, proto::EmptyPermSet>,
    proto::End,
    /*MinEpoch=*/7,
    /*MinGeneration=*/2>;

// Recipient ctx whose LoopCtx satisfies the carrier's epoch floor.
using EpochCtxType = proto::EpochExecCtx<7, 2, eff::HotFgCtx>;

// Transport callable with the carrier resource but returning void:
// requires clause `is_invocable_v<Transport, CarrierChannel&>` is true;
// std::invoke_result_t<Transport, CarrierChannel&> deduces to `void`,
// poisoning the body deduction in inner_.accept(...).
static void void_returning_transport(CarrierChannel&) noexcept {}

}  // namespace neg_mint_recording_session_psh_epoched_accept_transport_returns_void

int main() {
    using namespace
        neg_mint_recording_session_psh_epoched_accept_transport_returns_void;

    EpochCtxType ctx{};
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // Build the carrier PSH at the EpochedAccept state.  Epoch gate
    // passes because ctx is EpochExecCtx<7, 2, ...> >= floor 7,2.
    auto carrier = proto::mint_permissioned_session<CarrierProto>(
        ctx, CarrierChannel{});

    auto recording =
        proto::mint_recording_session(std::move(carrier), log, self, peer);

    // The forbidden call: Transport returns void; DelegatedResource
    // deduces to void; body instantiation of inner_.accept(...) fails.
    auto bad = std::move(recording).accept(void_returning_transport);
    (void)bad;
    return 0;
}
