// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_recording_endpoint wraps an endpoint.  A bare channel handle is not
// an endpoint: it was never bound to a context, so no gate has admitted
// its protocol, and the mint does not accept it.
//
// Expected diagnostic: no overload of mint_recording_endpoint takes a
// channel handle.

#include <fixy/concurrent/EndpointMint.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace recording_endpoint_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<Tag>>();
}
using Channel = c::spsc_channel_t<int, 8, decltype(channel_root())>;
}  // namespace recording_endpoint_fixture

int main() {
    using namespace recording_endpoint_fixture;
    namespace perm = ::foundation::permissions;
    namespace s = ::fixy::session;
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)consumer_perm;
    s::SessionEventLog log;
    auto recorded =
        c::mint_recording_endpoint(channel.producer(std::move(producer_perm)), log, s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
