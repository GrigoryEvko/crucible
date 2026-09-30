// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_producer_session asks for a permissioned MetaLog as its
// first template argument.  An int has none of the tags or handles, so the
// gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_not_a_log_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace producer_not_a_log_fixture

int main() {
    using Log = producer_not_a_log_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
            producer_not_a_log_fixture::log_root());
    (void)consumer_perm;
    auto producer = log.producer(std::move(producer_perm));
    auto head = ::crucible::metalog_session::mint_metalog_producer_session<int>(ctx, std::move(producer));
    (void)head;
    return 0;
}
