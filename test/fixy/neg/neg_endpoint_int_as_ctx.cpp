// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An endpoint binds a channel handle to an execution context.  An int is
// not an execution context, so mint_endpoint rejects the call.
//
// Expected diagnostic: IsExecCtx is not satisfied.

#include <fixy/concurrent/Endpoint.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace endpoint_ctx_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
using Channel = c::PermissionedSpscChannel<int, 8, Tag>;
}  // namespace endpoint_ctx_fixture

int main() {
    using namespace endpoint_ctx_fixture;
    namespace perm = ::foundation::permissions;
    Channel channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(
        perm::mint_permission_root<Channel::whole_tag>());
    (void)consumer_perm;
    const int not_a_ctx = 0;
    auto endpoint = c::mint_endpoint<Channel, c::Direction::Producer>(not_a_ctx, channel.producer(std::move(producer_perm)));
    return endpoint.try_send(1) ? 0 : 1;
}
