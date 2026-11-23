// Negative compile test.  This file must fail to compile.
//
// The bridge table names the permissioned channels only.  A type that looks
// like a channel, with a value type and a producer end, still has no row in
// the table, so mint_substrate_session rejects it.
//
// Expected diagnostic: end_for has no row for the fake channel.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace substrate_table_fixture {
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct FakeChannel {
    using value_type = int;
    struct ProducerEnd {
        [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    };
};
}  // namespace substrate_table_fixture

int main() {
    using namespace substrate_table_fixture;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    auto head =
        channels::mint_substrate_session<FakeChannel, channels::Direction::Producer>(ctx, FakeChannel::ProducerEnd{});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
