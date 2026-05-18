// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-010 substrate-side HS14 floor: closes the higher-order
// capability-transfer gap on `payload_row<DelegatedSession<InnerProto,
// InnerPS>>`.  Before the fix, the trait fell through to the primary
// template's `Row<>` default — sending a `DelegatedSession<P, IPS>`
// across a channel hid every effect P's Send/Recv operations would
// carry once the recipient ran P.  After the fix, the trait surfaces
// `protocol_effect_row_t<InnerProto>` so the outer Ctx must admit the
// union of P's effects, the same discipline that
// `payload_row<Capability<E, S>>` enforces for first-order caps.
//
// Violation: mint a session whose outer protocol is
//
//   Send<DelegatedSession<Send<Computation<Row<IO>, int>, End>,
//                         EmptyPermSet>, End>
//
// in a `HotFgCtx` whose row is empty.  Sending the DelegatedSession
// payload would transfer the capability to run an IO-bearing inner
// protocol — but the foreground Ctx admits no IO effect.  With the
// A2-010 fix, `payload_row<DelegatedSession<...>>` yields `Row<IO>`,
// and `proto_row_admitted_by<Send<…>, HotFgCtx>` rejects the mint at
// `CtxFitsProtocol`.  Without the fix, this would have silently
// admitted — recipient runs the inner protocol's IO Send under an
// unadmitted effect row.
//
// Pairs with `neg_delegated_session_payload_row_bg.cpp` (distinct
// rejection class: outer Ctx admits some but not all of the inner
// protocol's effects).
//
// Expected diagnostic: CtxFitsPermissionedProtocol / CtxFitsProtocol /
//                       constraints not satisfied /
//                       no matching function / EffectRowMismatch.

#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionDelegate.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_a2_010_fg {
struct Channel {};
}

int main() {
    using InnerProto =
        proto::Send<eff::Computation<eff::Row<eff::Effect::IO>, int>,
                    proto::End>;
    using IPS_empty = proto::EmptyPermSet;

    using OuterProto =
        proto::Send<proto::DelegatedSession<InnerProto, IPS_empty>,
                    proto::End>;

    eff::HotFgCtx ctx{};
    auto bad = proto::mint_permissioned_session<OuterProto>(
        ctx, neg_a2_010_fg::Channel{});
    (void)bad;
    return 0;
}
