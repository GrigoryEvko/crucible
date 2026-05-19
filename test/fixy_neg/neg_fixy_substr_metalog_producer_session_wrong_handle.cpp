// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #6/8:
// `fixy::substr::metalog::mint_metalog_producer_session<Log, Ctx>(
//      ctx, handle)` rejects when the second (handle) parameter
// cannot bind to `typename Log::ProducerHandle&`.
//
// `Log` is supplied explicitly and IS a known
// MetaLogSessionSurface.  `HotFgCtx` IS IsExecCtx.  Both concept
// gates pass; the second parameter `int` cannot bind to
// `Log::ProducerHandle&` (a class reference).
//
// Distinct from fixture #5 (producer_session_non_ctx): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_metalog_producer_session'" / "cannot convert" /
// "ProducerHandle" / "mint_metalog_producer_session".

#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fmeta = ::crucible::fixy::substr::metalog;
namespace eff   = ::crucible::effects;

namespace neg_fixy_meta_producer_session_wrong_handle {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fmeta::mint_metalog_producer_session<
        neg_fixy_meta_producer_session_wrong_handle::Log>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
