// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermissionedMetaLog::producer takes the Permission of the producer tag,
// and that parameter type is its whole admission.  This call passes the
// Permission of the consumer tag of the same root, so no producer handle
// can be made from it.

#include <crucible/MetaLog.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_wrong_permission_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace producer_wrong_permission_fixture

int main() {
    using Log = producer_wrong_permission_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [unused, wrong] = ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
        producer_wrong_permission_fixture::log_root());
    (void)unused;
    auto producer = log.producer(std::move(wrong));
    (void)producer;
    return 0;
}
