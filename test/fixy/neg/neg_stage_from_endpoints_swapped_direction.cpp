// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The stage body drains its first handle and fills its second.  Passing the
// producer endpoint first and the consumer endpoint second swaps the two
// directions, so mint_stage_from_endpoints rejects the call.
//
// Expected diagnostic: IsConsumerEndpoint is not satisfied.

#include <fixy/concurrent/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace stage_swap_fixture {
namespace c = ::fixy::concurrent;
struct InTag {};
struct OutTag {};
using In = c::PermissionedSpscChannel<int, 8, InTag>;
using Out = c::PermissionedSpscChannel<int, 8, OutTag>;
inline void body(In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}
}  // namespace stage_swap_fixture

int main() {
    using namespace stage_swap_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    Out out{};
    auto [in_p, in_c] =
        perm::mint_permission_split<In::producer_tag, In::consumer_tag>(perm::mint_permission_root<In::whole_tag>());
    auto [out_p, out_c] =
        perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(perm::mint_permission_root<Out::whole_tag>());
    (void)in_p;
    (void)out_c;
    auto in_ep = c::mint_endpoint<In, c::Direction::Consumer>(ctx, in.consumer(std::move(in_c)));
    auto out_ep = c::mint_endpoint<Out, c::Direction::Producer>(ctx, out.producer(std::move(out_p)));
    auto stage = c::mint_stage_from_endpoints<&body>(ctx, std::move(out_ep), std::move(in_ep));
    std::move(stage).run();
    return 0;
}
