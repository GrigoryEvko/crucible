// Negative-compile test.  This file must fail to compile.
//
// The producer door of PermissionedMetaLog takes the Permission of the
// producer tag, and that parameter type is its whole admission.  This call
// passes the Permission of the consumer tag of the same root, so the door
// opens no producer end from it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_wrong_permission_fixture {
struct Tag {};
}  // namespace producer_wrong_permission_fixture

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::crucible::metalog_tag::Whole<producer_wrong_permission_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<0>())));
    ::fixy::concurrent::with_producer(
        log, std::move(tokens.at(::foundation::core::index<1>())),
        [](auto producer) noexcept -> void { ::foundation::linear::drop(std::move(producer).release()); });
    return 0;
}
