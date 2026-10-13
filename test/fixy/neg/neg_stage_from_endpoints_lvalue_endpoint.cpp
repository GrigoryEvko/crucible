// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_stage_from_endpoints consumes its endpoints.  An endpoint passed as
// an lvalue would be moved from without a std::move at the call site, so
// the mint refuses it.
//
// Expected diagnostic: IsMovedEndpoint is not satisfied.

#include <fixy/channels/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace stage_lvalue_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct InTag {};
struct OutTag {};
inline auto in_root() noexcept { return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<InTag>>(); }
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<OutTag>>();
}
using In = c::spsc_channel_t<int, 8, decltype(in_root())>;
using Out = c::spsc_channel_t<int, 8, decltype(out_root())>;
inline void body(In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}
}  // namespace stage_lvalue_fixture

int main() {
    using namespace stage_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    Out out{};
    auto [in_p, in_c] = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    auto [out_p, out_c] = perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(out_root());
    (void)in_p;
    (void)out_c;
    auto in_ep = channels::mint_endpoint<In, channels::Direction::Consumer>(ctx, in.consumer(std::move(in_c)));
    auto out_ep = channels::mint_endpoint<Out, channels::Direction::Producer>(ctx, out.producer(std::move(out_p)));
    auto stage = channels::mint_stage_from_endpoints<&body>(ctx, in_ep, std::move(out_ep));
    std::move(stage).run();
    return 0;
}
