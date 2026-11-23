// Negative compile test.  This file must fail to compile.
//
// An endpoint binds a channel handle to an execution context.  An int is
// not an execution context, so mint_endpoint rejects the call.  The handle
// is the frame handle of the producer, so the mint stands in the body of the
// frame door.
//
// Expected diagnostic: IsExecCtx is not satisfied.

#include <fixy/channels/Endpoint.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace endpoint_ctx_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace endpoint_ctx_fixture

int main() {
    using namespace endpoint_ctx_fixture;
    namespace perm = ::foundation::permissions;
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    const int not_a_ctx = 0;
    return c::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [&not_a_ctx](auto producer) noexcept -> int {
            auto endpoint =
                channels::mint_endpoint<Channel, channels::Direction::Producer>(not_a_ctx, std::move(producer));
            return endpoint.try_send(1) ? 0 : 1;
        });
}
