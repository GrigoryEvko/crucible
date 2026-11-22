// Negative compile test.  This file must fail to compile.
//
// A unit opens the producer pole of a log through the pole door, ends the
// session by its local choice, and rewinds the handle at End to a protocol
// of its own that sends a long.  The log table names the protocol of the
// pole, and a rewind names a protocol of its own, so the two ends of the log
// would run protocols that the relation of the log does not admit.  So the
// gate of the rewind refuses a Resource that reaches a pole of a channel.
//
// Expected diagnostic: no rewind admits the handle, because the Resource
// reaches a pole of a channel.

#include <fixy/Ctx.h>
#include <fixy/channels/LocalLogSession.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace rewind_pole_fixture {
namespace s = ::fixy::session;
struct IntLog {
    using value_type = int;
    struct ProducerHandle {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
    struct ConsumerHandle {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
};
}  // namespace rewind_pole_fixture

int main() {
    using namespace rewind_pole_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, IntLog::ProducerHandle{});
    auto at_end = std::move(head).select<1>(s::no_label);
    auto rewound = s::HandleFactory::rewind<s::Loop<s::Send<long, s::Continue>>, void>(std::move(at_end));
    static_cast<void>(rewound);
    return 0;
}
