// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_metalog_producer_session asks for an execution context as its first
// argument.  An int is not one, so the gate refuses the call.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace producer_not_a_context_fixture {
struct Tag {};
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace producer_not_a_context_fixture

int main() {
    using Log = producer_not_a_context_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto producer = log.producer(::foundation::permissions::mint_permission_root<Log::producer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_producer_session<Log>(0, std::move(producer));
    (void)head;
    return 0;
}
