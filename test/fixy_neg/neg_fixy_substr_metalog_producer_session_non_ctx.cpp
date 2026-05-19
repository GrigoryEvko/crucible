// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #5/8:
// `fixy::substr::metalog::mint_metalog_producer_session<Log, Ctx>(
//      ctx, handle)` rejects when the first (ctx) parameter is
// NOT an IsExecCtx.
//
// `Log` is supplied explicitly (it appears in non-deduced
// position via `typename Log::ProducerHandle&`) and IS a known
// MetaLogSessionSurface.  The concept check on Log passes; `Ctx`
// is deduced from the first argument as `int`; `IsExecCtx`
// rejects it.
//
// Distinct from fixture #6 (producer_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite on the first parameter
// slot; #6 exercises the second (handle) parameter binding AFTER
// IsExecCtx is satisfied.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_metalog_producer_session".

#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/fixy/Substr.h>

namespace fmeta = ::crucible::fixy::substr::metalog;

namespace neg_fixy_meta_producer_session_non_ctx {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_meta_producer_session_non_ctx::Log::ProducerHandle* handle =
        nullptr;

    auto bad = fmeta::mint_metalog_producer_session<
        neg_fixy_meta_producer_session_non_ctx::Log>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
