// Negative-compile test.  This file must fail to compile.
//
// A MetaLog consumer endpoint drains only.  The consumer door of the log
// gives its body a MetaLogConsumerHandle, and the producer-side append
// method is structurally absent from it.

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
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<0>())));
    ::fixy::concurrent::with_consumer(log, std::move(tokens.at(::foundation::core::index<1>())),
                                      [](auto consumer) noexcept -> void {
                                          const ::fixy::FixedArray<::crucible::TensorMeta, 1> record{};
                                          [[maybe_unused]] auto start = consumer.try_append(record.view());
                                          ::foundation::linear::drop(std::move(consumer).release());
                                      });
    return 0;
}
