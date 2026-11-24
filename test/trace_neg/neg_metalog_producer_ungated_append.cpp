// Negative-compile test.  This file must fail to compile.
//
// Each append of a producer endpoint of PermissionedMetaLog takes the
// context of the caller, and the gate of the endpoint reads it.  An append
// with no context would skip the gate, so the endpoint has no such append.
//
// Expected diagnostic: the producer handle of the frame has no member
// try_append.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
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
                                          const ::fixy::FixedArray<::crucible::TensorMeta, 1> record{};
                                          (void)producer.try_append(record.view());
                                          ::foundation::linear::drop(std::move(producer).release());
                                      });
    return 0;
}
