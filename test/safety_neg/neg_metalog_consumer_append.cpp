// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-061 fixture #2 — MetaLog consumer endpoints drain only.  The
// producer-side append method is structurally absent from ConsumerHandle.

#include <crucible/MetaLog.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
using Log = ::crucible::concurrent::PermissionedMetaLog<Tag>;
}

int main() {
    ::crucible::MetaLog raw;
    Log log{raw};
    auto whole = ::crucible::safety::mint_permission_root<Log::whole_tag>();
    auto [pp, cp] = ::crucible::safety::mint_permission_split<
        Log::producer_tag, Log::consumer_tag>(std::move(whole));
    (void)pp;
    auto consumer = log.consumer(std::move(cp));
    const ::crucible::TensorMeta record{};
    [[maybe_unused]] auto start = consumer.try_append(&record, 1);
    return 0;
}
