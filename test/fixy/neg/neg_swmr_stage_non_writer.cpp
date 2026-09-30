// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A single-writer stage publishes into a writer, a handle with the publish
// shape.  The second argument here has no publish member, so it is not a
// writer, and mint_swmr_stage rejects the call.
//
// Expected diagnostic: CtxFitsSwmrStageFromEndpoint is not satisfied.

#include <fixy/concurrent/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_writer_fixture {
namespace c = ::fixy::concurrent;
struct InTag {};
inline auto in_root() noexcept { return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<InTag>>(); }
using In = c::spsc_channel_t<int, 8, decltype(in_root())>;
struct Writer {
    void publish(int const&) noexcept {}
};
struct NotAWriter {};
inline void body(In::ConsumerHandle&&, Writer&&) noexcept {}
}  // namespace swmr_writer_fixture

int main() {
    using namespace swmr_writer_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    auto [in_p, in_c] = perm::mint_permission_split<In::producer_tag, In::consumer_tag>(in_root());
    (void)in_p;
    auto stage = c::mint_swmr_stage<&body>(
        ctx, c::mint_endpoint<In, c::Direction::Consumer>(ctx, in.consumer(std::move(in_c))), NotAWriter{});
    std::move(stage).run();
    return 0;
}
