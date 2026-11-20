// The obligation is the row the region tag carries, and the caller
// presents a session handle.  A handle discharges a protocol position
// and says nothing about what a caller is permitted to do, so the gate
// refuses it.  The two kinds are one gate and not one obligation.

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
    auto borrow = storage.view();
    auto witnessed = s::mint_witnessed_under<Cache>(borrow);
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto at_send = s::mint_session<Sending>(ctx, Wire{});
    [[maybe_unused]] auto read = deref(witnessed, at_send);
    (void)std::move(at_send)
        .send(Ping{1},
              [](Wire& w, Ping& p) noexcept {
                  w.last_sent = p.value;
                  return true;
              })
        .close();
    return 0;
}
