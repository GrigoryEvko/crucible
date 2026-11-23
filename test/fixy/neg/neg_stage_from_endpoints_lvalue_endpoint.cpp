// Negative compile test.  This file must fail to compile.
//
// mint_stage_from_endpoints consumes its endpoints.  An endpoint passed as
// an lvalue would be moved from without a std::move at the call site, so
// the mint refuses it.  The endpoints run over the frame handles of two
// nested frame doors, so the mint stands in the inner body.
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
inline auto in_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, InTag>>();
}
inline auto out_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, OutTag>>();
}
using In = c::channel_t<int, 8, decltype(in_root())>;
using Out = c::channel_t<int, 8, decltype(out_root())>;
// A frame handle carries the scope brand of its frame, so the body of the
// stage takes the handle types as parameters.
template <class Drain, class Fill>
void body(Drain&&, Fill&&) noexcept {}
}  // namespace stage_lvalue_fixture

int main() {
    using namespace stage_lvalue_fixture;
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
         out_token = std::move(out_tokens.at(::foundation::core::index<0>()))](auto drain) mutable noexcept {
            c::with_producer(out, std::move(out_token), [&ctx, drain = std::move(drain)](auto fill) mutable noexcept {
                auto in_ep = channels::mint_endpoint<In, channels::Direction::Consumer>(ctx, std::move(drain));
                auto out_ep = channels::mint_endpoint<Out, channels::Direction::Producer>(ctx, std::move(fill));
                auto stage = channels::mint_stage_from_endpoints<&body<decltype(drain), decltype(fill)>>(
                    ctx, in_ep, std::move(out_ep));
                std::move(stage).run();
            });
        });
    return 0;
}
