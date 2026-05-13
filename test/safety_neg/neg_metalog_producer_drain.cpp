// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-061 fixture #1 — MetaLog producer endpoints append only.  The
// consumer-side drain method is structurally absent from ProducerHandle.

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
    (void)cp;
    auto producer = log.producer(std::move(pp));
    [[maybe_unused]] auto record = producer.try_drain_one();
    return 0;
}
