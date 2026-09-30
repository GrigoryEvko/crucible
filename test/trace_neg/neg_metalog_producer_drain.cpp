// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A MetaLog producer endpoint appends only.  The consumer-side drain method
// is structurally absent from ProducerHandle.

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
    (void)cp;
    auto producer = log.producer(std::move(pp));
    [[maybe_unused]] auto record = producer.try_drain_one();
    return 0;
}
