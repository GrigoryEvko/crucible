// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A rewind of a channel claims the two records of the pair with one
// priority, as a channel mint does.  The two Resources below state two
// priorities, so they are no two ends of one channel, and the rewind of the
// pair refuses them.
//
// Expected diagnostic: no rewind of a channel accepts the two Resources.
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_rewind_channel_priorities_differ_types {
struct LowWire {
    int words = 0;
};
struct HighWire {
    static constexpr ::fixy::session::watch::priority session_priority{1};
    int words = 0;
};
}  // namespace neg_sess_rewind_channel_priorities_differ_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_rewind_channel_priorities_differ_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto low_at_end = s::mint_session<s::End>(ctx, LowWire{});
    auto high_at_end = s::mint_session<s::End>(ctx, HighWire{});
    auto rewound = s::HandleFactory::rewind_channel<s::End, void>(std::move(low_at_end), std::move(high_at_end));
    static_cast<void>(rewound);
    return 0;
}
