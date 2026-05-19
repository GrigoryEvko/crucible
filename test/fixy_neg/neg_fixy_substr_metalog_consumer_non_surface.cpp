// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #3/8:
// `fixy::substr::metalog::mint_metalog_consumer<Log>(log, perm)`
// rejects when Log is NOT a MetaLogSessionSurface.
//
// Mirrors fixture #1 (producer_non_surface) on the consumer
// side: proves the MetaLogSessionSurface concept gate is
// preserved through the using-decl in Substr.h INDEPENDENTLY of
// the producer-side instantiation.
//
// Distinct from fixture #4 (consumer_wrong_perm): #3 exercises
// the concept gate on the first (Log) parameter; #4 exercises
// the second (perm) parameter binding AFTER the concept gate
// succeeds.
//
// Expected diagnostic: "MetaLogSessionSurface" / "constraints
// not satisfied" / "no matching function" / "mint_metalog_consumer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fmeta = ::crucible::fixy::substr::metalog;
namespace saf   = ::crucible::safety;

struct consumer_tag_placeholder {};

int main() {
    int not_a_log = 0;
    auto perm = saf::mint_permission_root<consumer_tag_placeholder>();

    auto bad = fmeta::mint_metalog_consumer(not_a_log, std::move(perm));
    (void)bad;
    return 0;
}
