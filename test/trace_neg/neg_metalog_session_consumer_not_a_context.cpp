// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_consumer_session asks for an execution context as its first
// argument.  An int is not one, so the gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace consumer_not_a_context_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace consumer_not_a_context_fixture

int main() {
    using Log = consumer_not_a_context_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
            consumer_not_a_context_fixture::log_root());
    (void)producer_perm;
    auto consumer = log.consumer(std::move(consumer_perm));
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(0, std::move(consumer));
    (void)head;
    return 0;
}
