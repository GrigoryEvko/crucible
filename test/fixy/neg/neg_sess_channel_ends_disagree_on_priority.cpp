// The two ends of a channel are one session, so they have one priority.
// If the two Resources stated different priorities, a thread could hold
// one end under a low priority while its peer waits on the other end
// under a high one, and the order of fixy/session/Watch.h would not hold
// across the channel.  The channel mint refuses the pair.

#include <fixy/session/Handle.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct LowEnd {
    int last_sent = 0;
};
struct HighEnd {
    static constexpr s::watch::priority session_priority{2};
    int last_sent = 0;
};
using Proto = s::Send<int, s::End>;
}  // namespace

int main() {
    const eff::detail::ctx_witnesses::TestRunnerCtx ctx{eff::testing::test()};
    auto [sender, receiver] = s::mint_test_channel<Proto>(ctx, LowEnd{}, HighEnd{});
    std::move(sender).detach(s::detach_reason::TestInstrumentation{});
    std::move(receiver).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
