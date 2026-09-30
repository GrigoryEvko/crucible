// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermissionedMetaLog::consumer takes the Permission of the consumer tag,
// and that parameter type is its whole admission.  This call passes the
// Permission of the producer tag of the same root, so no consumer handle
// can be made from it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_wrong_permission_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace consumer_wrong_permission_fixture

int main() {
    using Log = consumer_wrong_permission_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [wrong, unused] = ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
        consumer_wrong_permission_fixture::log_root());
    (void)unused;
    auto consumer = log.consumer(std::move(wrong));
    (void)consumer;
    return 0;
}
