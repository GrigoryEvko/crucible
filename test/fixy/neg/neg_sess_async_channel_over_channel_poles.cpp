// Negative compile test.  This file must fail to compile.
//
// A unit forks an asynchronous channel whose two Resources each hold a pole
// of an SPSC channel of int, and state a capacity.  mint_forked_async_channel
// names its two protocols, and the table of the SPSC channel names others.
// So its gate refuses a Resource that reaches a pole of a channel, as the
// gate of mint_session does.  A pole enters a session only through the pole
// door, which takes the two protocols from the table of the channel.  The
// poles are the frame handles of the two ends, so the fork stands in the body
// of the frame door.
//
// Expected diagnostic: no mint_forked_async_channel admits the Resources,
// because each one reaches a pole of a channel.

#include <fixy/Ctx.h>
#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace async_pole_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct Tag {};
struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;

// A frame handle carries the scope brand of its frame, so each wrapper takes
// that brand as a parameter.  The brand names no channel, so only the member
// holds the pole.
template <class Scope>
struct HeldProducer {
    static constexpr std::size_t channel_capacity = 1;
    Channel::producer_handle<Scope> pole;
};
template <class Scope>
struct HeldConsumer {
    static constexpr std::size_t channel_capacity = 1;
    Channel::consumer_handle<Scope> pole;
};

using BgCtx = ::fixy::BgDrainCtx;
struct Ping {};
struct Pong {};
using LeftProto = ::fixy::session::Send<Ping, ::fixy::session::Recv<Pong, ::fixy::session::End>>;
using RightProto = ::fixy::session::Recv<Ping, ::fixy::session::Send<Pong, ::fixy::session::End>>;

struct AnyBody {
    template <typename Head, typename View>
    auto operator()(Head head, View const&, BgCtx const&) noexcept {
        return head;
    }
};
}  // namespace async_pole_fixture

template <>
struct foundation::permissions::can_split_into_pack<async_pole_fixture::Whole, async_pole_fixture::Left,
                                                    async_pole_fixture::Right> : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<async_pole_fixture::Whole, async_pole_fixture::Left,
                                                                 async_pole_fixture::Right> : std::true_type {};

int main() {
    using namespace async_pole_fixture;
    namespace s = ::fixy::session;
    namespace perm = ::foundation::permissions;
    const BgCtx ctx{eff::testing::bg()};
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_ends(channel, std::move(tokens.at(::foundation::core::index<0>())),
                 std::move(tokens.at(::foundation::core::index<1>())), [&ctx](auto producer, auto consumer) noexcept {
                     auto back = s::mint_forked_async_channel<LeftProto, RightProto, Left, Right>(
                         ctx, perm::mint_permission_root<Whole>(),
                         HeldProducer<typename decltype(producer)::scope_type>{.pole = std::move(producer)},
                         HeldConsumer<typename decltype(consumer)::scope_type>{.pole = std::move(consumer)}, AnyBody{},
                         AnyBody{});
                     ::foundation::linear::drop(std::move(back));
                 });
    return 0;
}
