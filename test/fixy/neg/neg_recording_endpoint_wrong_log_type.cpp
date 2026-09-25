// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The recorder writes each step to a session event log.  An int is not a
// log, so mint_recording_endpoint does not accept it.
//
// Expected diagnostic: no overload of mint_recording_endpoint takes an int
// as the log.

#include <fixy/concurrent/EndpointMint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace recording_log_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
using Channel = c::PermissionedSpscChannel<int, 8, Tag>;
}  // namespace recording_log_fixture

int main() {
    using namespace recording_log_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(
        perm::mint_permission_root<Channel::whole_tag>());
    (void)consumer_perm;
    int not_a_log = 0;
    auto recorded = c::mint_recording_endpoint(
        c::mint_endpoint<Channel, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm))), not_a_log,
        s::RoleTagId{1}, s::RoleTagId{2});
    (void)recorded;
    return 0;
}
