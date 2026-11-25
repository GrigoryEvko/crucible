// Negative compile test.  This file must fail to compile.
//
// mint_recording_endpoint wraps an endpoint.  A bare channel handle is not
// an endpoint: it was never bound to a context, so no gate has admitted
// its protocol, and the mint does not accept it.  The handle is the frame
// handle of the producer, so the call stands in the body of the frame door.
//
// Expected diagnostic: no overload of mint_recording_endpoint takes a
// channel handle.

#include <fixy/channels/EndpointMint.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace recording_endpoint_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace recording_endpoint_fixture

int main() {
    using namespace recording_endpoint_fixture;
    namespace perm = ::foundation::permissions;
    namespace s = ::fixy::session;
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    s::SessionEventLog log;
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [&log](auto producer) noexcept {
        auto recorded = channels::mint_recording_endpoint(
            std::move(producer), s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log),
            s::RoleTagId{1}, s::RoleTagId{2});
        (void)recorded;
    });
    return 0;
}
