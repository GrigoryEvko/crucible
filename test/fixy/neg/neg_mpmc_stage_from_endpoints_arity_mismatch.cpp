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
inline auto in_root() noexcept { return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<InTag>>(); }
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<OutTag>>();
}
using In = c::spsc_channel_t<int, 8, decltype(in_root())>;
using Out = c::spsc_channel_t<int, 8, decltype(out_root())>;
inline void fan_in(In::ConsumerHandle&&, In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}
}  // namespace mpmc_arity_fixture

int main() {
    using namespace mpmc_arity_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    Out out{};
    auto [in_p, in_c] = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    auto [out_p, out_c] = perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(out_root());
    (void)in_p;
    (void)out_c;
    auto stage = c::mint_mpmc_stage_from_endpoints<&fan_in>(
        ctx, c::mint_endpoint<In, c::Direction::Consumer>(ctx, in.consumer(std::move(in_c))),
        c::mint_endpoint<Out, c::Direction::Producer>(ctx, out.producer(std::move(out_p))));
    std::move(stage).run();
    return 0;
}
