// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-090 #2245 — HS14 mint-gate witness #2/2 for
// `fixy::substr::mpsc::mint_mpsc_consumer_session<Channel, Ctx>(
//      ctx, handle)`.
//
// Violation: pass a non-ExecCtx as the first parameter.  The ctx-bound
// mint is gated by `effects::IsExecCtx Ctx` on the template parameter;
// passing `int` for ctx fails the concept gate before parameter slot 2
// is even examined.
//
// Pairs with neg_fixy_substr_mpsc_consumer_session_wrong_handle.cpp
// (parameter slot 2 ConsumerHandle binding failure AFTER IsExecCtx
// passes).  Distinct mismatch classes per HS14 §XXI:
//   #1 (peer) — slot 2 handle-type binding
//   #2 (this) — slot 1 IsExecCtx concept gate
//
// Expected diagnostic: "constraints not satisfied" / "IsExecCtx" /
// "no matching function" / "mint_mpsc_consumer_session".

#include <crucible/fixy/Substr.h>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_consumer_session_non_ctx {
struct UserTag {};
using Channel = fmpsc::PermissionedMpscChannel<int, 64, UserTag>;
}  // namespace neg_fixy_mpsc_consumer_session_non_ctx

int main() {
    int not_a_ctx = 0;
    neg_fixy_mpsc_consumer_session_non_ctx::Channel::ConsumerHandle*
        consumer_handle = nullptr;

    auto bad = fmpsc::mint_mpsc_consumer_session<
        neg_fixy_mpsc_consumer_session_non_ctx::Channel>(
            not_a_ctx, *consumer_handle);
    (void)bad;
    return 0;
}
