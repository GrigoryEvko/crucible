// Negative-compile test.  This file must fail to compile.
//
// A unit mints a plain session over the producer handle of the MetaLog, with
// a protocol of its own that loops on a send and never stops.  The handle
// binds the log, so it is a pole of a channel, and the log table names the
// protocol of each of its poles.  A mint of one end names one protocol, so
// its gate refuses a Resource that reaches a pole.  A session opens on the
// log only through the pole door, on the protocols of the log table.
//
// Expected diagnostic: no mint_session admits the handle, because the
// Resource reaches a pole of a channel.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/session/Entry.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace plain_handle_fixture {
struct Tag {};
inline auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<Tag>>();
}
using Log = ::crucible::permissioned_metalog_t<decltype(log_root())>;
}  // namespace plain_handle_fixture

int main() {
    namespace s = ::fixy::session;
    using Log = plain_handle_fixture::Log;
    const auto ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(
            plain_handle_fixture::log_root());
    (void)consumer_perm;
    auto head = s::mint_session<s::Loop<s::Send<::crucible::TensorMeta, s::Continue>>>(
        ctx, log.producer(std::move(producer_perm)));
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
