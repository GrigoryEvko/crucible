// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The two ends of one channel run dual protocols, so the loops around their
// two handles at End face each other.  The first handle below stands at End
// in a loop, and the second one stands at End in no loop.  Their types show
// that they are the ends of two channels, so the rewind of the pair refuses
// them.
//
// Expected diagnostic: no rewind of a channel accepts the two loop contexts.
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_rewind_channel_loops_differ_types {
struct LoopWire {
    int words = 0;
};
struct PlainWire {
    int words = 0;
};
using Repeats = ::fixy::session::Loop<
    ::fixy::session::Select<::fixy::session::Send<int, ::fixy::session::Continue>, ::fixy::session::End>>;
}  // namespace neg_sess_rewind_channel_loops_differ_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_rewind_channel_loops_differ_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto in_loop = s::mint_session<Repeats>(ctx, LoopWire{}).select_local<1>(ctx);
    auto in_no_loop = s::mint_session<s::End>(ctx, PlainWire{});
    auto rewound = s::HandleFactory::rewind_channel<s::End, void>(std::move(in_loop), std::move(in_no_loop));
    static_cast<void>(rewound);
    return 0;
}
