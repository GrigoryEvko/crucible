// The callback entry takes the execution context of the caller, as each
// session mint does.  This call gives the Resource and the body with no
// context, so the entry is not viable, and no effect of the protocol
// escapes the gate.

#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace with_without_ctx_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using ReceivesIo = s::Recv<IoWork, s::End>;
}  // namespace with_without_ctx_fixture

int main() {
    auto back = ::fixy::session::with_session<with_without_ctx_fixture::ReceivesIo>(
        with_without_ctx_fixture::Channel{}, [](auto head) noexcept { return head; });
    (void)back;
    return 0;
}
