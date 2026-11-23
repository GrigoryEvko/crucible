// Negative-compile test.  This file must fail to compile.
//
// A unit mints a plain session over the producer handle of a frame of the
// MetaLog, with a protocol of its own that loops on a send and never stops.
// The handle binds the log, so it is a pole of a channel, and the log table
// names the protocol of each of its poles.  A mint of one end names one
// protocol, so its gate refuses a Resource that reaches a pole.  A session
// opens on the log only through the pole door, on the protocols of the log
// table.
//
// Expected diagnostic: no mint_session admits the handle, because the
// Resource reaches a pole of a channel.

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/session/Entry.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace plain_handle_fixture {
struct Tag {};
}  // namespace plain_handle_fixture

int main() {
    namespace s = ::fixy::session;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto root =
        ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<plain_handle_fixture::Tag>>();
    using Log = ::crucible::permissioned_metalog_t<decltype(root)>;
    ::crucible::MetaLog raw{::foundation::effects::testing::test().alloc};
    Log log{raw};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Log::producer_tag, Log::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(tokens.at(::foundation::core::index<1>())));
    ::fixy::concurrent::with_producer(
        log, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept -> void {
            auto head =
                s::mint_session<s::Loop<s::Send<::crucible::TensorMeta, s::Continue>>>(ctx, std::move(producer));
            std::move(head).detach(s::detach_reason::TestInstrumentation{});
        });
    return 0;
}
