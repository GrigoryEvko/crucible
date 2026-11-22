// Negative compile test.  This file must fail to compile.
//
// A unit records a session over the producer pole of a log, and it picks a
// branch of the Select of the log with a write of a word of its own.  A
// select over a pole of a channel puts no word on the wire and takes only
// no_label, so the recorded handle refuses the write of the caller, as the
// handle that it wraps does.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the select of the
// recorded handle.

#include <fixy/Ctx.h>
#include <fixy/channels/LocalLogSession.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Recording.h>

#include <cstddef>
#include <utility>

namespace recorded_pole_select_fixture {
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
}  // namespace recorded_pole_select_fixture

int main() {
    using namespace recorded_pole_select_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    s::SessionEventLog log;
    auto recorded = s::mint_recorded_session(
        s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, IntLog::ProducerHandle{}),
        s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
    auto at_end = std::move(recorded).select<1>([](IntLog::ProducerHandle&, std::size_t) noexcept { return true; });
    std::move(at_end).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
