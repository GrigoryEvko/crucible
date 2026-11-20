// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Each append of a producer endpoint of PermissionedMetaLog takes the
// context of the caller, and the gate of the endpoint reads it.  An append
// with no context would skip the gate, so the endpoint has no such append.
//
// Expected diagnostic: the producer handle has no member try_append.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace

int main() {
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto [pp, cp] = ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(log_root());
    (void)cp;
    auto producer = log.producer(std::move(pp));
    const ::fixy::FixedArray<::crucible::TensorMeta, 1> record{};
    (void)producer.try_append(record.view());
    return 0;
}
