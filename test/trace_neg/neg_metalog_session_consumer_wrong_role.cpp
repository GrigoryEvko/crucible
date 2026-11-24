// Negative-compile test.  This file must fail to compile.
//
// mint_metalog_consumer_session runs the draining protocol, so it takes the
// consumer handle of a frame.  This call passes the producer handle of the
// producer door, and the gate refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_wrong_role_fixture {
struct Tag {};
}  // namespace consumer_wrong_role_fixture

int main() {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto root = ::foundation::permissions::mint_permission_root<
        ::crucible::metalog_tag::Whole<consumer_wrong_role_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    Log log{::foundation::effects::testing::test().alloc};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_producer(
        log, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept -> void {
            auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(ctx, std::move(producer));
            (void)head;
        });
    return 0;
}
