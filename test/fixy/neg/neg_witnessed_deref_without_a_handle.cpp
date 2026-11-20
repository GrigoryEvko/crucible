// The obligation is a protocol position, and the caller presents an
// execution context.  A context discharges a row and says nothing about
// where a session stands, so the gate refuses it.

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
    const ::fixy::TestRunnerCtx session_ctx{::foundation::effects::testing::test()};
    auto at_send = s::mint_session<Sending>(session_ctx, Wire{});
    auto borrow = storage.view();
    auto witnessed = s::mint_witnessed_at(at_send, borrow);
    FgCtx ctx = eff::testing::foreground();
    [[maybe_unused]] auto read = deref(witnessed, ctx);
    (void)std::move(at_send)
        .send(Ping{1},
              [](Wire& w, Ping& p) noexcept {
                  w.last_sent = p.value;
                  return true;
              })
        .close();
    return 0;
}
