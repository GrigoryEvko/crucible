// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-090 #2245 — HS14 mint-gate witness #2/2 for
// `fixy::substr::mpsc::mint_mpsc_producer_session<Channel, Ctx>(
//      ctx, handle)`.
//
// Violation: pass `Channel::ConsumerHandle&` to the producer-session
// mint whose second parameter is `Channel::ProducerHandle&`.  The
// IsExecCtx gate (slot 1) succeeds — HotFgCtx is a real ExecCtx — but
// slot 2 cannot bind ConsumerHandle to ProducerHandle& (distinct
// nested types within the same Channel).
//
// Pairs with neg_fixy_substr_mpsc_producer_session_non_ctx.cpp (slot 1
// IsExecCtx gate).  Distinct mismatch classes per HS14 §XXI:
//   #1 (peer) — slot 1 IsExecCtx concept gate
//   #2 (this) — slot 2 ProducerHandle/ConsumerHandle binding
//
// Expected diagnostic: "could not convert" / "cannot bind reference" /
// "no matching function" / "ProducerHandle" /
// "mint_mpsc_producer_session".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_producer_session_wrong_handle {
struct UserTag {};
using Channel = fmpsc::PermissionedMpscChannel<int, 64, UserTag>;
}  // namespace neg_fixy_mpsc_producer_session_wrong_handle

int main() {
    ::crucible::effects::HotFgCtx const ctx{};
    neg_fixy_mpsc_producer_session_wrong_handle::Channel::ConsumerHandle*
        consumer_handle = nullptr;

    auto bad = fmpsc::mint_mpsc_producer_session<
        neg_fixy_mpsc_producer_session_wrong_handle::Channel>(
            ctx, *consumer_handle);
    (void)bad;
    return 0;
}
