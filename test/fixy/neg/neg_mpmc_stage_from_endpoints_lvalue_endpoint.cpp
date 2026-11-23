// Negative compile test.  This file must fail to compile.
//
// mint_mpmc_stage_from_endpoints consumes its endpoints.  One endpoint is
// passed as an lvalue, which would be moved from without a std::move at
// the call site, so the mint refuses the call.
//
// Expected diagnostic: CtxFitsMpmcStageFromEndpoints is not satisfied.

#include <fixy/channels/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace mpmc_lvalue_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct LeftTag {};
struct RightTag {};
struct OutTag {};
inline auto left_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LeftTag>>();
}
inline auto right_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, RightTag>>();
}
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, OutTag>>();
}
using Left = c::channel_t<int, 8, decltype(left_root())>;
using Right = c::channel_t<int, 8, decltype(right_root())>;
using Out = c::channel_t<int, 8, decltype(out_root())>;
// Each handle carries the scope brand of its frame, so the body is a
// template over the types of its handles.
template <class LeftIn, class RightIn, class Output>
void fan_in(LeftIn&&, RightIn&&, Output&&) noexcept {}
}  // namespace mpmc_lvalue_fixture

int main() {
    using namespace mpmc_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Left left{};
    Right right{};
    Out out{};
    auto left_tokens = perm::mint_permission_split<Left::producer_tag, Left::consumer_tag>(left_root());
    auto right_tokens = perm::mint_permission_split<Right::producer_tag, Right::consumer_tag>(right_root());
    auto out_tokens = perm::mint_permission_split<Out::producer_tag, Out::consumer_tag>(out_root());
    c::with_consumer(
        left, std::move(left_tokens.at(::foundation::core::index<1>())),
        [&ctx, &right, &out, right_consumer = std::move(right_tokens.at(::foundation::core::index<1>())),
         out_producer = std::move(out_tokens.at(::foundation::core::index<0>()))](auto left_in) mutable noexcept {
            c::with_consumer(
                right, std::move(right_consumer),
                [&ctx, &out, out_producer = std::move(out_producer),
                 left_in = std::move(left_in)](auto right_in) mutable noexcept {
                    c::with_producer(
                        out, std::move(out_producer),
                        [&ctx, left_in = std::move(left_in),
                         right_in = std::move(right_in)](auto out_handle) mutable noexcept {
                            auto left_ep =
                                channels::mint_endpoint<Left, channels::Direction::Consumer>(ctx, std::move(left_in));
                            auto stage = channels::mint_mpmc_stage_from_endpoints<
                                &fan_in<decltype(left_in), decltype(right_in), decltype(out_handle)>>(
                                ctx, left_ep,
                                channels::mint_endpoint<Right, channels::Direction::Consumer>(ctx, std::move(right_in)),
                                channels::mint_endpoint<Out, channels::Direction::Producer>(ctx,
                                                                                            std::move(out_handle)));
                            std::move(stage).run();
                        });
                });
        });
    return 0;
}
