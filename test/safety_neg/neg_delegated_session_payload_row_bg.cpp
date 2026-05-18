// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-010 substrate-side HS14 floor: closes the higher-order
// capability-transfer gap on `payload_row<DelegatedSession<InnerProto,
// InnerPS>>`.  Distinct rejection class from
// `neg_delegated_session_payload_row_fg.cpp` (which exercises a Fg
// empty-row Ctx).  Here the outer Ctx is `BgCompileCtx`, whose row is
// `Row<Bg, Alloc, IO>` — strictly missing `Block`.  The inner protocol's
// Send/Recv chain carries `Block`, so the union over the inner Sends'
// payload rows is `Row<Block>`, NOT a subrow of `Row<Bg, Alloc, IO>`.
//
// Before the A2-010 fix, the empty-row default on
// `payload_row<DelegatedSession<...>>` silently passed the subset
// check (Row<> is a subrow of every row).  After the fix the trait
// returns `protocol_effect_row_t<InnerProto>` = `Row<Block>`, and the
// subset check at `proto_row_admitted_by<Send<DelegatedSession<...>, K>,
// BgCompileCtx>` rejects the mint at `CtxFitsProtocol`.
//
// Pairs with `neg_delegated_session_payload_row_fg.cpp` (different
// rejection class: outer Ctx admits no effects at all vs. admits a
// proper subset).
//
// Expected diagnostic: CtxFitsPermissionedProtocol / CtxFitsProtocol /
//                       constraints not satisfied /
//                       no matching function / EffectRowMismatch.

#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionDelegate.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

namespace neg_a2_010_bg {
struct Channel {};
}

int main() {
    // Inner protocol: a single Send of a Block-rowed payload (i.e.,
    // running it would block on IO/lock acquisition under the
    // delegate's Ctx).  Block is in the canonical Effect universe but
    // is NOT a member of BgCompileCtx::row_type = Row<Bg, Alloc, IO>.
    using InnerProto =
        proto::Send<eff::Computation<eff::Row<eff::Effect::Block>, int>,
                    proto::End>;
    using IPS_empty = proto::EmptyPermSet;

    // Outer protocol sends the DelegatedSession payload.  Without the
    // A2-010 fix, the outer Send's payload_row was Row<> (the default)
    // — silent admission under any Ctx, including BgCompileCtx.  With
    // the fix, it's protocol_effect_row<InnerProto> = Row<Block>,
    // which is not a subrow of Row<Bg, Alloc, IO>.
    using OuterProto =
        proto::Send<proto::DelegatedSession<InnerProto, IPS_empty>,
                    proto::End>;

    eff::BgCompileCtx ctx{};
    auto bad = proto::mint_permissioned_session<OuterProto>(
        ctx, neg_a2_010_bg::Channel{});
    (void)bad;
    return 0;
}
