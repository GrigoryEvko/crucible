// Negative compile test.  This file must fail to compile.
//
// A single-writer stage publishes into a writer, a handle with the publish
// shape.  The second argument here has no publish member, so it is not a
// writer, and mint_swmr_stage_from_endpoint rejects the call.
//
// Expected diagnostic: CtxFitsSwmrStageFromEndpoint is not satisfied.

#include <fixy/channels/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_writer_fixture {
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
struct NotAWriter {};
// The consumer handle carries the scope brand of its frame, so the body is
// a template over the type of the handle.
template <class Input>
void body(Input&&, Writer&&) noexcept {}
}  // namespace swmr_writer_fixture

int main() {
    using namespace swmr_writer_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    auto in_tokens = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    c::with_consumer(in, std::move(in_tokens.at(::foundation::core::index<1>())), [&ctx](auto in_handle) noexcept {
        auto stage = channels::mint_swmr_stage_from_endpoint<&body<decltype(in_handle)>>(
            ctx, channels::mint_endpoint<In, channels::Direction::Consumer>(ctx, std::move(in_handle)), NotAWriter{});
        std::move(stage).run();
    });
    return 0;
}
