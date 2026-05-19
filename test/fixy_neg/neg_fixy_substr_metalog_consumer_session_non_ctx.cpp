// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #7/8:
// `fixy::substr::metalog::mint_metalog_consumer_session<Log, Ctx>(
//      ctx, handle)` rejects when the first (ctx) parameter is
// NOT an IsExecCtx.
//
// Mirrors fixture #5 (producer_session_non_ctx) on the consumer
// side: proves the IsExecCtx prerequisite is preserved through
// the using-decl INDEPENDENTLY of the producer-side
// instantiation.
//
// Distinct from fixture #8 (consumer_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_metalog_consumer_session".

#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/fixy/Substr.h>

namespace fmeta = ::crucible::fixy::substr::metalog;

namespace neg_fixy_meta_consumer_session_non_ctx {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_meta_consumer_session_non_ctx::Log::ConsumerHandle* handle =
        nullptr;

    auto bad = fmeta::mint_metalog_consumer_session<
        neg_fixy_meta_consumer_session_non_ctx::Log>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
