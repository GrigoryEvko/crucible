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

#include <utility>

namespace consumer_lvalue_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace consumer_lvalue_fixture

int main() {
    using Log = consumer_lvalue_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
            consumer_lvalue_fixture::log_root());
    (void)producer_perm;
    auto consumer = log.consumer(std::move(consumer_perm));
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(ctx, consumer);
    (void)head;
    return 0;
}
