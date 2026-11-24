// Negative-compile test.  This file must fail to compile.
//
// mint_metalog_consumer_session asks for an execution context as its first
// argument.  An int is not one, so the gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_not_a_context_fixture {
struct Tag {};
}  // namespace consumer_not_a_context_fixture

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::crucible::metalog_tag::Whole<consumer_not_a_context_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    Log log{::foundation::effects::testing::test().alloc};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<0>())));
    ::fixy::concurrent::with_consumer(
        log, std::move(tokens.at(::foundation::core::index<1>())), [](auto consumer) noexcept -> void {
            auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(0, std::move(consumer));
            (void)head;
        });
    return 0;
}
