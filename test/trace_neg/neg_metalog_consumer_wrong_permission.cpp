// Negative-compile test.  This file must fail to compile.
//
// The consumer door of PermissionedMetaLog takes the Permission of the
// consumer tag, and that parameter type is its whole admission.  This call
// passes the Permission of the producer tag of the same root, so the door
// opens no consumer end from it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_wrong_permission_fixture {
struct Tag {};
}  // namespace consumer_wrong_permission_fixture

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::crucible::metalog_tag::Whole<consumer_wrong_permission_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    Log log{::foundation::effects::testing::test().alloc};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_consumer(
        log, std::move(tokens.at(::foundation::core::index<0>())),
        [](auto consumer) noexcept -> void { ::foundation::linear::drop(std::move(consumer).release()); });
    return 0;
}
