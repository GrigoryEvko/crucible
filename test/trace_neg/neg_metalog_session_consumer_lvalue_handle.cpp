// Negative-compile test.  This file must fail to compile.
//
// The session owns the channel handle, so mint_metalog_consumer_session
// takes it by move.  This call passes the handle of the frame as an lvalue,
// which would leave the body a second way to reach the log while the session
// runs.  The gate refuses it.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_lvalue_fixture {
struct Tag {};
}  // namespace consumer_lvalue_fixture

int main() {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto root =
        ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<consumer_lvalue_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<0>())));
    ::fixy::concurrent::with_consumer(
        log, std::move(tokens.at(::foundation::core::index<1>())), [&ctx](auto consumer) noexcept -> void {
            auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(ctx, consumer);
            (void)head;
            ::foundation::linear::drop(std::move(consumer).release());
        });
    return 0;
}
