// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The session owns the channel handle, so mint_metalog_producer_session
// takes it by move.  This call passes the handle as an lvalue, which would
// leave the caller a second way to reach the log while the session runs.
// The gate refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace producer_lvalue_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace producer_lvalue_fixture

int main() {
    using Log = producer_lvalue_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto producer = log.producer(::foundation::permissions::mint_permission_root<Log::producer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_producer_session<Log>(ctx, producer);
    (void)head;
    return 0;
}
