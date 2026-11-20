// The handle is a temporary, so the protocol position it names is gone
// at the end of the full expression and the borrow would be witnessed
// against a session that no longer exists.  The deleted rvalue twin
// says that rather than leaving the binding error to say it.

#include <fixy/session/Witnessed.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = ::foundation::effects;
namespace s = ::fixy::session;

namespace {
struct Cache {
    using permission_row = eff::Row<>;
};
struct Spilled {
    using permission_row = eff::Row<eff::Effect::IO>;
};
struct Unrowed {};
struct Ping {
    int value = 0;
};
struct Wire {
    int last_sent = 0;
};
using Sending = s::Send<Ping, s::End>;
using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
::fixy::FixedArray<std::uint64_t, 3> storage{1, 2, 3};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto borrow = storage.view();
    [[maybe_unused]] auto witnessed = s::mint_witnessed_at(s::mint_session<Sending>(ctx, Wire{}), borrow);
    return 0;
}
