// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermissionedMetaLog::consumer takes the Permission of the consumer tag,
// and that parameter type is its whole admission.  This call passes the
// Permission of the producer tag, so no consumer handle can be made from
// it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_wrong_permission_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace consumer_wrong_permission_fixture

int main() {
    using Log = consumer_wrong_permission_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto wrong = ::foundation::permissions::mint_permission_root<Log::producer_tag>();
    auto consumer = log.consumer(std::move(wrong));
    (void)consumer;
    return 0;
}
