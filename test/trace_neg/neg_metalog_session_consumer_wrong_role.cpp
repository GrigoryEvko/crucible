// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_consumer_session runs the draining protocol, so it takes the
// consumer handle.  This call passes the producer handle, and the gate
// refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_wrong_role_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace consumer_wrong_role_fixture

int main() {
    using Log = consumer_wrong_role_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
            consumer_wrong_role_fixture::log_root());
    (void)consumer_perm;
    auto producer = log.producer(std::move(producer_perm));
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(ctx, std::move(producer));
    (void)head;
    return 0;
}
