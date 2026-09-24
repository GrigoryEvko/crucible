// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermissionedMetaLog::producer takes the Permission of the producer tag,
// and that parameter type is its whole admission.  This call passes the
// Permission of the consumer tag, so no producer handle can be made from
// it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_wrong_permission_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace producer_wrong_permission_fixture

int main() {
    using Log = producer_wrong_permission_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto wrong = ::foundation::permissions::mint_permission_root<Log::consumer_tag>();
    auto producer = log.producer(std::move(wrong));
    (void)producer;
    return 0;
}
