// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A single-writer stage drains a consumer endpoint.  A producer endpoint
// has no pop, so it cannot feed the stage, and mint_swmr_stage rejects the
// call.
//
// Expected diagnostic: CtxFitsSwmrStageFromEndpoint is not satisfied.

#include <fixy/concurrent/StageEndpointBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace swmr_direction_fixture {
namespace c = ::fixy::concurrent;
struct InTag {};
using In = c::PermissionedSpscChannel<int, 8, InTag>;
struct Writer {
    void publish(int const&) noexcept {}
};
inline void body(In::ConsumerHandle&&, Writer&&) noexcept {}
}  // namespace swmr_direction_fixture

int main() {
    using namespace swmr_direction_fixture;
    namespace perm = ::foundation::permissions;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    In in{};
    auto [in_p, in_c] =
        perm::mint_permission_split<In::producer_tag, In::consumer_tag>(perm::mint_permission_root<In::whole_tag>());
    (void)in_c;
    auto stage = c::mint_swmr_stage<&body>(
        ctx, c::mint_endpoint<In, c::Direction::Producer>(ctx, in.producer(std::move(in_p))), Writer{});
    std::move(stage).run();
    return 0;
}
