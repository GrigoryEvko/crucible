// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A MetaLog consumer endpoint gives no reference into a slot.  After the
// consumer releases a record, the producer writes a new record into its slot,
// so the endpoint copies a record with copy_at.  The member at is absent.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

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
    auto consumer = log.consumer(
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(log_root()).second);
    [[maybe_unused]] const auto& record = consumer.at(::crucible::MetaIndex{0});
    return 0;
}
