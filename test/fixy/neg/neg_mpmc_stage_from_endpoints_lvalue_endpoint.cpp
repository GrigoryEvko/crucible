// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_mpmc_stage_from_endpoints consumes its endpoints.  One endpoint is
// passed as an lvalue, which would be moved from without a std::move at
// the call site, so the mint refuses the call.
//
// Expected diagnostic: CtxFitsMpmcStageFromEndpoints is not satisfied.

#include <fixy/concurrent/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace mpmc_lvalue_fixture {
namespace c = ::fixy::concurrent;
struct LeftTag {};
struct RightTag {};
struct OutTag {};
inline auto left_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<LeftTag>>();
}
inline auto right_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<RightTag>>();
}
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<OutTag>>();
}
using Left = c::spsc_channel_t<int, 8, decltype(left_root())>;
using Right = c::spsc_channel_t<int, 8, decltype(right_root())>;
using Out = c::spsc_channel_t<int, 8, decltype(out_root())>;
inline void fan_in(Left::ConsumerHandle&&, Right::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}
}  // namespace mpmc_lvalue_fixture

int main() {
    using namespace mpmc_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Left left{};
    Right right{};
    Out out{};
    auto [left_p, left_c] = perm::mint_permission_split<Left::producer_tag, Left::consumer_tag>(left_root());
    auto [right_p, right_c] = perm::mint_permission_split<Right::producer_tag, Right::consumer_tag>(right_root());
    auto [out_p, out_c] = perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(out_root());
    (void)left_p;
    (void)right_p;
    (void)out_c;
    auto left_ep = c::mint_endpoint<Left, c::Direction::Consumer>(ctx, left.consumer(std::move(left_c)));
    auto stage = c::mint_mpmc_stage_from_endpoints<&fan_in>(
        ctx, left_ep, c::mint_endpoint<Right, c::Direction::Consumer>(ctx, right.consumer(std::move(right_c))),
        c::mint_endpoint<Out, c::Direction::Producer>(ctx, out.producer(std::move(out_p))));
    std::move(stage).run();
    return 0;
}
