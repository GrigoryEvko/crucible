// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-20 negative fixture #1/2:
// `fixy::substr::mpsc::mint_mpsc_producer_session<Channel,
//  Ctx>(ctx, handle)` rejects when the first (ctx) parameter is
// NOT an IsExecCtx.  Channel is supplied explicitly (it appears in
// non-deducible position via `typename Channel::ProducerHandle&`)
// and IS a known MpscChannelSessionSurface.  The Channel concept
// check passes; `Ctx` is deduced from the first argument as `int`;
// `IsExecCtx` rejects it.
//
// Distinct from fixture #2 (consumer_session_wrong_handle): #1
// exercises the IsExecCtx prerequisite (first parameter slot,
// before handle binding); #2 exercises the ConsumerHandle reference
// binding (second parameter slot) AFTER IsExecCtx succeeds.  The
// two fixtures together discharge HS14's ≥2-distinct-mismatch
// requirement on the fixy::substr::mpsc:: session-mint family.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_mpsc_producer_session".

#include <crucible/fixy/Substr.h>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_producer_session_non_ctx {
struct UserTag {};
using Channel = fmpsc::PermissionedMpscChannel<int, 64, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_mpsc_producer_session_non_ctx::Channel::ProducerHandle* handle =
        nullptr;

    auto bad = fmpsc::mint_mpsc_producer_session<
        neg_fixy_mpsc_producer_session_non_ctx::Channel>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
