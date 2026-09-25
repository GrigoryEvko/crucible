// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The bridge table names the SPSC and the MPSC channel only.  A type that
// looks like a channel, with a value type and a producer handle, still has
// no row in the table, so mint_substrate_session rejects it.
//
// Expected diagnostic: handle_for has no row for the fake channel.

#include <fixy/concurrent/SubstrateSessionBridge.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace substrate_table_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct FakeChannel {
    using value_type = int;
    struct ProducerHandle {
        [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    };
};
}  // namespace substrate_table_fixture

int main() {
    using namespace substrate_table_fixture;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    auto head = c::mint_substrate_session<FakeChannel, c::Direction::Producer>(ctx, FakeChannel::ProducerHandle{});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
