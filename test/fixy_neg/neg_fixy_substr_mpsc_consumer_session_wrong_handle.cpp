// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-20 negative fixture #2/2:
// `fixy::substr::mpsc::mint_mpsc_consumer_session<Channel,
//  Ctx>(ctx, handle)` rejects when the handle parameter is a
// ProducerHandle reference rather than the required ConsumerHandle
// reference.  The IsExecCtx gate succeeds (HotFgCtx is a real
// ExecCtx alias); the second parameter slot fails to bind because
// `typename Channel::ConsumerHandle&` cannot reference a
// `Channel::ProducerHandle` lvalue.
//
// Distinct from fixture #1 (producer_session_non_ctx): #2 exercises
// the consumer-side handle-type binding (parameter slot 2) AFTER
// IsExecCtx succeeds; #1 exercises the IsExecCtx prerequisite
// (parameter slot 1).  Together the two fixtures discharge HS14's
// ≥2-distinct-mismatch floor on the fixy::substr::mpsc:: session-
// mint family.
//
// Expected diagnostic: "could not convert" / "cannot bind reference"
// / "no matching function" / "mint_mpsc_consumer_session" /
// "ConsumerHandle".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_consumer_session_wrong_handle {
struct UserTag {};
using Channel = fmpsc::PermissionedMpscChannel<int, 64, UserTag>;
}

int main() {
    ::crucible::effects::HotFgCtx const ctx{};
    neg_fixy_mpsc_consumer_session_wrong_handle::Channel::ProducerHandle*
        producer_handle = nullptr;

    auto bad = fmpsc::mint_mpsc_consumer_session<
        neg_fixy_mpsc_consumer_session_wrong_handle::Channel>(
            ctx, *producer_handle);
    (void)bad;
    return 0;
}
