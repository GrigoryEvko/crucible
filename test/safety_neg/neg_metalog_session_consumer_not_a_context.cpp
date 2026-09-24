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
using Log = ::crucible::PermissionedMetaLog<Tag>;
}  // namespace consumer_not_a_context_fixture

int main() {
    using Log = consumer_not_a_context_fixture::Log;
    ::crucible::MetaLog raw;
    Log log{raw};
    auto consumer = log.consumer(::foundation::permissions::mint_permission_root<Log::consumer_tag>());
    auto head = ::crucible::metalog_session::mint_metalog_consumer_session<Log>(0, std::move(consumer));
    (void)head;
    return 0;
}
