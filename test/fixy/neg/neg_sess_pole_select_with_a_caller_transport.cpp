// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit opens the producer pole of a log through the pole door, and it
// picks a branch of the Select of the log with a write of a word of its own.
// The log carries records and no label, so a select over a pole of a channel
// puts no word on the wire, and it takes only no_label.  A write of the
// caller would put a label word into the log in a second way.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the select of the
// handle.

#include <fixy/Ctx.h>
#include <fixy/channels/LocalLogSession.h>
#include <fixy/session/Handle.h>

#include <cstddef>
#include <utility>

namespace pole_select_fixture {
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
}  // namespace pole_select_fixture

int main() {
    using namespace pole_select_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, IntLog::ProducerHandle{});
    auto at_end = std::move(head).select<1>([](IntLog::ProducerHandle&, std::size_t) noexcept { return true; });
    std::move(at_end).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
