// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #8/8:
// `fixy::substr::metalog::mint_metalog_consumer_session<Log, Ctx>(
//      ctx, handle)` rejects when the second (handle) parameter
// cannot bind to `typename Log::ConsumerHandle&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle reference binding is
// preserved through the using-decl INDEPENDENTLY of the
// producer-side instantiation.
//
// Distinct from fixture #7 (consumer_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_metalog_consumer_session'" / "cannot convert" /
// "ConsumerHandle" / "mint_metalog_consumer_session".

#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fmeta = ::crucible::fixy::substr::metalog;
namespace eff   = ::crucible::effects;

namespace neg_fixy_meta_consumer_session_wrong_handle {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fmeta::mint_metalog_consumer_session<
        neg_fixy_meta_consumer_session_wrong_handle::Log>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
