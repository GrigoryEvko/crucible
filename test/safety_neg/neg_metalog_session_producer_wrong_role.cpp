// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_producer_session runs the appending protocol, so it takes
// the producer handle.  This call passes the consumer handle, and the gate
// refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_wrong_role_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace producer_wrong_role_fixture

int main() {
    using Log = producer_wrong_role_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto consumer = log.consumer(::foundation::permissions::mint_permission_root<Log::consumer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_producer_session<Log>(ctx, std::move(consumer));
    (void)head;
    return 0;
}
