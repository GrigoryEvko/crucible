// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_consumer_session asks for a permissioned MetaLog as its
// first template argument.  An int has none of the tags or handles, so the
// gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_not_a_log_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace consumer_not_a_log_fixture

int main() {
    using Log = consumer_not_a_log_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto consumer = log.consumer(::foundation::permissions::mint_permission_root<Log::consumer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<int>(ctx, std::move(consumer));
    (void)head;
    return 0;
}
