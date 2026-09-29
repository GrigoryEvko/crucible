// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The session owns the channel handle, so mint_metalog_consumer_session
// takes it by move.  This call passes the handle as an lvalue, which would
// leave the caller a second way to reach the log while the session runs.
// The gate refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace consumer_lvalue_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace consumer_lvalue_fixture

int main() {
    using Log = consumer_lvalue_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto consumer = log.consumer(::foundation::permissions::mint_permission_root<Log::consumer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(ctx, consumer);
    (void)head;
    return 0;
}
