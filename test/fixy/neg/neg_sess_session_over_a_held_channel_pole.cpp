// Negative compile test.  This file must fail to compile.
//
// A unit wraps the producer pole of an SPSC channel in a class of its own,
// which names no channel, and mints a plain session over that class with a
// protocol of its own.  The gate of a mint of one end walks the Resource,
// and the walk finds the pole in a member.  So the wrapper does not hide the
// pole, and the two ends of the channel cannot run protocols that are not
// duals through it.
//
// Expected diagnostic: no mint_session admits the wrapper, because the
// Resource reaches a pole of a channel.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace held_pole_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
// The frame handle carries the scope brand of its frame, so the wrapper
// takes that brand as a parameter.  The brand names no channel, so only the
// member holds the pole.
template <class Scope>
struct HeldPole {
    Channel::producer_handle<Scope> pole;
};
}  // namespace held_pole_fixture

int main() {
    using namespace held_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept {
        auto session = s::mint_session<s::Loop<s::Send<long, s::Continue>>>(
            ctx, HeldPole<typename decltype(producer)::scope_type>{.pole = std::move(producer)});
        std::move(session).detach(s::detach_reason::TestInstrumentation{});
    });
    return 0;
}
