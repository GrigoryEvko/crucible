// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol sends a computation that needs Bg.  The test context holds
// Test, Alloc, IO and Block, and no Bg.  The test hatch gives the two
// endpoints of the channel to one caller under one context, so the
// context must hold each effect that a payload of either side carries,
// and mint_test_channel refuses the call.
//
// Expected diagnostic: CtxAdmitsChannelRow is not satisfied.

#include <fixy/session/Handle.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

namespace test_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
using BgWork = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Proto = s::Send<BgWork, s::End>;
using TestCtx = eff::detail::ctx_witnesses::TestRunnerCtx;
}  // namespace test_row_fixture

int main() {
    using namespace test_row_fixture;
    const TestCtx ctx{eff::testing::test()};
    auto both = s::mint_test_channel<Proto>(ctx, Wire{}, Wire{});
    std::move(both.first).detach(s::detach_reason::TestInstrumentation{});
    std::move(both.second).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
