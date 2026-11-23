// Negative compile test.  This file must fail to compile.
//
// The fan-in body takes two consumer handles and one producer handle.  Only
// one consumer endpoint and one producer endpoint are passed, so the
// endpoints do not match the parameters of the body, and
// mint_mpmc_stage_from_endpoints rejects the call.
//
// Expected diagnostic: CtxFitsMpmcStageFromEndpoints is not satisfied.

#include <fixy/channels/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace mpmc_arity_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct InTag {};
struct OutTag {};
inline auto in_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, InTag>>();
}
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, OutTag>>();
}
using In = c::channel_t<int, 8, decltype(in_root())>;
using Out = c::channel_t<int, 8, decltype(out_root())>;
// Each handle carries the scope brand of its frame, so the body is a
// template over the types of its handles.
template <class Left, class Right, class Output>
void fan_in(Left&&, Right&&, Output&&) noexcept {}
}  // namespace mpmc_arity_fixture

int main() {
    using namespace mpmc_arity_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    Out out{};
    auto in_tokens = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    auto out_tokens = perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(out_root());
    c::with_consumer(
        in, std::move(in_tokens.at(::foundation::core::index<1>())),
        [&ctx, &out,
         out_producer = std::move(out_tokens.at(::foundation::core::index<0>()))](auto in_handle) mutable noexcept {
            c::with_producer(
                out, std::move(out_producer),
                [&ctx, in_handle = std::move(in_handle)](auto out_handle) mutable noexcept {
                    auto stage = channels::mint_mpmc_stage_from_endpoints<
                        &fan_in<decltype(in_handle), decltype(in_handle), decltype(out_handle)>>(
                        ctx, channels::mint_endpoint<In, channels::Direction::Consumer>(ctx, std::move(in_handle)),
                        channels::mint_endpoint<Out, channels::Direction::Producer>(ctx, std::move(out_handle)));
                    std::move(stage).run();
                });
        });
    return 0;
}
