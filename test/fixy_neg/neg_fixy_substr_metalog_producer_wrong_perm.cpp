// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #2/8:
// `fixy::substr::metalog::mint_metalog_producer<Log>(log, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Log::producer_tag>&&`.
//
// Distinct from fixture #1 (producer_non_surface): #1 exercises
// the MetaLogSessionSurface concept gate on the first (Log)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// `PermissionedMetaLog<UserTag>` is a known MetaLogSessionSurface
// (witnessed by the static_assert at MetaLogSession.h:136).  The
// first parameter binds; the concept passes; the second
// parameter `int` cannot bind to `Permission<producer_tag>&&` (a
// class-typed rvalue reference).
//
// Expected diagnostic: "no matching function for call to
// 'mint_metalog_producer'" / "cannot convert" / "Permission" /
// "mint_metalog_producer".

#include <crucible/MetaLog.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fmeta = ::crucible::fixy::substr::metalog;

namespace neg_fixy_meta_producer_wrong_perm {
struct UserTag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<UserTag>;
}

int main() {
    ::crucible::MetaLog underlying{};
    neg_fixy_meta_producer_wrong_perm::Log log{underlying};
    int not_a_perm = 0;

    auto bad = fmeta::mint_metalog_producer(log, not_a_perm);
    (void)bad;
    return 0;
}
