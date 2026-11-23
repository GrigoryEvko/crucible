// Negative compile test.  This file must fail to compile.
//
// The select of the handle factory is the step that the select of a handle
// calls.  A select over a pole of a channel puts no word on the wire, so the
// factory refuses a write of the caller on a pole, as the select of the
// handle does.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the select of the handle
// factory.

#include <fixy/Ctx.h>
#include <fixy/channels/LocalLogSession.h>
#include <fixy/session/Handle.h>

#include <cstddef>
#include <utility>

namespace factory_pole_select_fixture {
namespace s = ::fixy::session;
struct IntLog {
    using value_type = int;
    struct ProducerEnd {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
    struct ConsumerEnd {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
};
}  // namespace factory_pole_select_fixture

int main() {
    using namespace factory_pole_select_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, IntLog::ProducerEnd{});
    auto at_end =
        s::HandleFactory::select<1>(std::move(head), [](IntLog::ProducerEnd&, std::size_t) noexcept { return true; });
    std::move(at_end).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
