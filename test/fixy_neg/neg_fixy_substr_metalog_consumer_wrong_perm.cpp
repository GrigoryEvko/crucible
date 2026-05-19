// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #4/8:
// `fixy::substr::metalog::mint_metalog_consumer<Log>(log, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Log::consumer_tag>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// proves the per-mint parameter shape is preserved through the
// using-decl INDEPENDENTLY of the producer-side instantiation.
//
// `PermissionedMetaLog<UserTag>` is a known MetaLogSessionSurface.
// The first parameter binds; the concept passes; the second
// parameter `int` cannot bind to `Permission<consumer_tag>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_metalog_consumer'" / "cannot convert" / "Permission" /
// "mint_metalog_consumer".

#include <crucible/MetaLog.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fmeta = ::crucible::fixy::substr::metalog;

namespace neg_fixy_meta_consumer_wrong_perm {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    ::crucible::MetaLog underlying{};
    neg_fixy_meta_consumer_wrong_perm::Log log{underlying};
    int not_a_perm = 0;

    auto bad = fmeta::mint_metalog_consumer(log, not_a_perm);
    (void)bad;
    return 0;
}
