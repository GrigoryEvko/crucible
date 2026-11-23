// Negative compile test.  This file must fail to compile.
//
// A single-writer stage drains a consumer endpoint.  A producer endpoint
// has no pop, so it cannot feed the stage, and mint_swmr_stage_from_endpoint
// rejects the call.  The frame opens the two ends of the channel, so the
// body names the consumer handle of the frame, and only the direction of
// the endpoint is wrong.
//
// Expected diagnostic: CtxFitsSwmrStageFromEndpoint is not satisfied.

#include <fixy/channels/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_direction_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
struct InTag {};
inline auto in_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, InTag>>();
}
using In = c::channel_t<int, 8, decltype(in_root())>;
struct Writer {
    void publish(int const&) noexcept {}
};
// The consumer handle carries the scope brand of its frame, so the body is
// a template over the type of the handle.
template <class Input>
void body(Input&&, Writer&&) noexcept {}
}  // namespace swmr_direction_fixture

int main() {
    using namespace swmr_direction_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    auto in_tokens = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    auto consumer_token = c::with_ends(
        in, std::move(in_tokens.at(::foundation::core::index<0>())),
        std::move(in_tokens.at(::foundation::core::index<1>())), [&ctx](auto producer, auto consumer) noexcept {
            auto stage = channels::mint_swmr_stage_from_endpoint<&body<decltype(consumer)>>(
                ctx, channels::mint_endpoint<In, channels::Direction::Producer>(ctx, std::move(producer)), Writer{});
            std::move(stage).run();
            return std::move(consumer).release();
        });
    return 0;
}
