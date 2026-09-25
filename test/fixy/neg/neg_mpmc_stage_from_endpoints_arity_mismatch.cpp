// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fan-in body takes two consumer handles and one producer handle.  Only
// one consumer endpoint and one producer endpoint are passed, so the
// endpoints do not match the parameters of the body, and
// mint_mpmc_stage_from_endpoints rejects the call.
//
// Expected diagnostic: CtxFitsMpmcStageFromEndpoints is not satisfied.

#include <fixy/concurrent/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace mpmc_arity_fixture {
namespace c = ::fixy::concurrent;
struct InTag {};
struct OutTag {};
using In = c::PermissionedSpscChannel<int, 8, InTag>;
using Out = c::PermissionedSpscChannel<int, 8, OutTag>;
inline void fan_in(In::ConsumerHandle&&, In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}
}  // namespace mpmc_arity_fixture

int main() {
    using namespace mpmc_arity_fixture;
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
    auto stage = c::mint_mpmc_stage_from_endpoints<&fan_in>(
        ctx, c::mint_endpoint<In, c::Direction::Consumer>(ctx, in.consumer(std::move(in_c))),
        c::mint_endpoint<Out, c::Direction::Producer>(ctx, out.producer(std::move(out_p))));
    std::move(stage).run();
    return 0;
}
