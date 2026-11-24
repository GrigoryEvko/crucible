// Negative-compile test.  This file must fail to compile.
//
// A MetaLog producer endpoint appends only.  The producer door of the log
// gives its body a MetaLogProducerHandle, and the consumer-side drain method
// is structurally absent from it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
}  // namespace

int main() {
    auto root = ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    Log log{::foundation::effects::testing::test().alloc};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_producer(log, std::move(tokens.at(::foundation::core::index<0>())),
                                      [](auto producer) noexcept -> void {
                                          [[maybe_unused]] auto record = producer.try_drain_one();
                                          ::foundation::linear::drop(std::move(producer).release());
                                      });
    return 0;
}
