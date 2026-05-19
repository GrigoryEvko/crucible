// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-08 negative fixture #1/8:
// `fixy::substr::metalog::mint_metalog_producer<Log>(log, perm)`
// rejects when Log is NOT a MetaLogSessionSurface.
//
// `int` lacks the MetaLogSessionSurface concept's required
// nested types (producer_tag, consumer_tag, ProducerHandle,
// ConsumerHandle, value_type) and the producer/consumer factory
// members.  The requires-clause fires at substitution time.
//
// Distinct from fixture #2 (wrong_perm): #1 exercises the
// MetaLogSessionSurface concept gate on the first (Log)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// Expected diagnostic: "MetaLogSessionSurface" / "constraints
// not satisfied" / "no matching function" / "mint_metalog_producer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fmeta = ::crucible::fixy::substr::metalog;
namespace saf   = ::crucible::safety;

struct producer_tag_placeholder {};

int main() {
    int not_a_log = 0;
    auto perm = saf::mint_permission_root<producer_tag_placeholder>();

    auto bad = fmeta::mint_metalog_producer(not_a_log, std::move(perm));
    (void)bad;
    return 0;
}
