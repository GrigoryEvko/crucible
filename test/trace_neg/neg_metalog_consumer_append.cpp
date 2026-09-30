// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A MetaLog consumer endpoint drains only.  The producer-side append method
// is structurally absent from ConsumerHandle.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

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
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [pp, cp] = ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(log_root());
    (void)pp;
    auto consumer = log.consumer(std::move(cp));
    const ::crucible::TensorMeta record{};
    [[maybe_unused]] auto start = consumer.try_append(&record, 1);
    return 0;
}
