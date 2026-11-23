// Negative-compile test.  This file must fail to compile.
//
// mint_metalog_producer_session asks for a permissioned MetaLog as its
// first template argument.  An int has none of the tags or handles, so the
// gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_not_a_log_fixture {
struct Tag {};
}  // namespace producer_not_a_log_fixture

int main() {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto root = ::foundation::permissions::mint_permission_root<
        ::crucible::metalog_tag::Whole<producer_not_a_log_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_producer(
        log, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept -> void {
            auto head = ::crucible::metalog_session::mint_metalog_producer_session<int>(ctx, std::move(producer));
            (void)head;
        });
    return 0;
}
