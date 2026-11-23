// Negative compile test.  This file must fail to compile.
//
// A unit mints a crash session over the consumer pole of an SPSC channel,
// with a protocol of its own.  A crash session is a session of one end, and
// it cannot see the protocol of the producer end.  So its gate refuses a
// Resource that reaches a pole of a channel, as the gate of mint_session
// does.  The pole is the frame handle of the consumer, so the mint stands in
// the body of the frame door.
//
// Expected diagnostic: no mint_crash_session admits the pole, because the
// Resource reaches a pole of a channel.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/CrashTransport.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace crash_pole_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
struct Alice {};
struct Bob {};
}  // namespace crash_pole_fixture

int main() {
    using namespace crash_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    using Proto = s::Offer<s::Recv<long, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_consumer(channel, std::move(tokens.at(::foundation::core::index<1>())), [&ctx](auto consumer) noexcept {
        s::CrashCell<Alice, Bob> own;
        const s::CrashCell<Bob, Alice> peer;
        auto handle =
            s::mint_crash_session<Proto, Alice, Bob>(ctx, std::move(consumer), peer, s::mint_crash_writer(own));
        static_cast<void>(handle);
    });
    return 0;
}
